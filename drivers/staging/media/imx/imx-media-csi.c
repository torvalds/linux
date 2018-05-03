/*
 * V4L2 Capture CSI Subdev for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2014-2017 Mentor Graphics Inc.
 * Copyright (C) 2017 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <video/imx-ipu-v3.h>
#include <media/imx.h>
#include "imx-media.h"

/*
 * Min/Max supported width and heights.
 *
 * We allow planar output, so we have to align width by 16 pixels
 * to meet IDMAC alignment requirements.
 *
 * TODO: move this into pad format negotiation, if capture device
 * has not requested planar formats, we should allow 8 pixel
 * alignment.
 */
#define MIN_W       176
#define MIN_H       144
#define MAX_W      4096
#define MAX_H      4096
#define W_ALIGN    4 /* multiple of 16 pixels */
#define H_ALIGN    1 /* multiple of 2 lines */
#define S_ALIGN    1 /* multiple of 2 */

/*
 * struct csi_skip_desc - CSI frame skipping descriptor
 * @keep - number of frames kept per max_ratio frames
 * @max_ratio - width of skip_smfc, written to MAX_RATIO bitfield
 * @skip_smfc - skip pattern written to the SKIP_SMFC bitfield
 */
struct csi_skip_desc {
	u8 keep;
	u8 max_ratio;
	u8 skip_smfc;
};

struct csi_priv {
	struct device *dev;
	struct ipu_soc *ipu;
	struct imx_media_dev *md;
	struct v4l2_subdev sd;
	struct media_pad pad[CSI_NUM_PADS];
	/* the video device at IDMAC output pad */
	struct imx_media_video_dev *vdev;
	struct imx_media_fim *fim;
	int csi_id;
	int smfc_id;

	/* lock to protect all members below */
	struct mutex lock;

	int active_output_pad;

	struct ipuv3_channel *idmac_ch;
	struct ipu_smfc *smfc;
	struct ipu_csi *csi;

	struct v4l2_mbus_framefmt format_mbus[CSI_NUM_PADS];
	const struct imx_media_pixfmt *cc[CSI_NUM_PADS];
	struct v4l2_fract frame_interval[CSI_NUM_PADS];
	struct v4l2_rect crop;
	struct v4l2_rect compose;
	const struct csi_skip_desc *skip;

	/* active vb2 buffers to send to video dev sink */
	struct imx_media_buffer *active_vb2_buf[2];
	struct imx_media_dma_buf underrun_buf;

	int ipu_buf_num;  /* ipu double buffer index: 0-1 */

	/* the sink for the captured frames */
	struct media_entity *sink;
	enum ipu_csi_dest dest;
	/* the source subdev */
	struct v4l2_subdev *src_sd;

	/* the mipi virtual channel number at link validate */
	int vc_num;

	/* the upstream endpoint CSI is receiving from */
	struct v4l2_fwnode_endpoint upstream_ep;

	spinlock_t irqlock; /* protect eof_irq handler */
	struct timer_list eof_timeout_timer;
	int eof_irq;
	int nfb4eof_irq;

	struct v4l2_ctrl_handler ctrl_hdlr;

	int stream_count; /* streaming counter */
	bool last_eof;   /* waiting for last EOF at stream off */
	bool nfb4eof;    /* NFB4EOF encountered during streaming */
	struct completion last_eof_comp;
};

static inline struct csi_priv *sd_to_dev(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi_priv, sd);
}

static inline bool is_parallel_16bit_bus(struct v4l2_fwnode_endpoint *ep)
{
	return ep->bus_type != V4L2_MBUS_CSI2 &&
		ep->bus.parallel.bus_width >= 16;
}

/*
 * Parses the fwnode endpoint from the source pad of the entity
 * connected to this CSI. This will either be the entity directly
 * upstream from the CSI-2 receiver, or directly upstream from the
 * video mux. The endpoint is needed to determine the bus type and
 * bus config coming into the CSI.
 */
static int csi_get_upstream_endpoint(struct csi_priv *priv,
				     struct v4l2_fwnode_endpoint *ep)
{
	struct device_node *endpoint, *port;
	struct media_entity *src;
	struct v4l2_subdev *sd;
	struct media_pad *pad;

	if (!priv->src_sd)
		return -EPIPE;

	src = &priv->src_sd->entity;

	if (src->function == MEDIA_ENT_F_VID_MUX) {
		/*
		 * CSI is connected directly to video mux, skip up to
		 * CSI-2 receiver if it is in the path, otherwise stay
		 * with video mux.
		 */
		sd = imx_media_find_upstream_subdev(priv->md, src,
						    IMX_MEDIA_GRP_ID_CSI2);
		if (!IS_ERR(sd))
			src = &sd->entity;
	}

	/* get source pad of entity directly upstream from src */
	pad = imx_media_find_upstream_pad(priv->md, src, 0);
	if (IS_ERR(pad))
		return PTR_ERR(pad);

	sd = media_entity_to_v4l2_subdev(pad->entity);

	/*
	 * NOTE: this assumes an OF-graph port id is the same as a
	 * media pad index.
	 */
	port = of_graph_get_port_by_id(sd->dev->of_node, pad->index);
	if (!port)
		return -ENODEV;

	endpoint = of_get_next_child(port, NULL);
	of_node_put(port);
	if (!endpoint)
		return -ENODEV;

	v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint), ep);
	of_node_put(endpoint);

	return 0;
}

static void csi_idmac_put_ipu_resources(struct csi_priv *priv)
{
	if (priv->idmac_ch)
		ipu_idmac_put(priv->idmac_ch);
	priv->idmac_ch = NULL;

	if (priv->smfc)
		ipu_smfc_put(priv->smfc);
	priv->smfc = NULL;
}

static int csi_idmac_get_ipu_resources(struct csi_priv *priv)
{
	int ch_num, ret;
	struct ipu_smfc *smfc;
	struct ipuv3_channel *idmac_ch;

	ch_num = IPUV3_CHANNEL_CSI0 + priv->smfc_id;

	smfc = ipu_smfc_get(priv->ipu, ch_num);
	if (IS_ERR(smfc)) {
		v4l2_err(&priv->sd, "failed to get SMFC\n");
		ret = PTR_ERR(smfc);
		goto out;
	}
	priv->smfc = smfc;

	idmac_ch = ipu_idmac_get(priv->ipu, ch_num);
	if (IS_ERR(idmac_ch)) {
		v4l2_err(&priv->sd, "could not get IDMAC channel %u\n",
			 ch_num);
		ret = PTR_ERR(idmac_ch);
		goto out;
	}
	priv->idmac_ch = idmac_ch;

	return 0;
out:
	csi_idmac_put_ipu_resources(priv);
	return ret;
}

static void csi_vb2_buf_done(struct csi_priv *priv)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	struct imx_media_buffer *done, *next;
	struct vb2_buffer *vb;
	dma_addr_t phys;

	done = priv->active_vb2_buf[priv->ipu_buf_num];
	if (done) {
		vb = &done->vbuf.vb2_buf;
		vb->timestamp = ktime_get_ns();
		vb2_buffer_done(vb, priv->nfb4eof ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	}

	priv->nfb4eof = false;

	/* get next queued buffer */
	next = imx_media_capture_device_next_buf(vdev);
	if (next) {
		phys = vb2_dma_contig_plane_dma_addr(&next->vbuf.vb2_buf, 0);
		priv->active_vb2_buf[priv->ipu_buf_num] = next;
	} else {
		phys = priv->underrun_buf.phys;
		priv->active_vb2_buf[priv->ipu_buf_num] = NULL;
	}

	if (ipu_idmac_buffer_is_ready(priv->idmac_ch, priv->ipu_buf_num))
		ipu_idmac_clear_buffer(priv->idmac_ch, priv->ipu_buf_num);

	ipu_cpmem_set_buffer(priv->idmac_ch, priv->ipu_buf_num, phys);
}

static irqreturn_t csi_idmac_eof_interrupt(int irq, void *dev_id)
{
	struct csi_priv *priv = dev_id;

	spin_lock(&priv->irqlock);

	if (priv->last_eof) {
		complete(&priv->last_eof_comp);
		priv->last_eof = false;
		goto unlock;
	}

	if (priv->fim)
		/* call frame interval monitor */
		imx_media_fim_eof_monitor(priv->fim, ktime_get());

	csi_vb2_buf_done(priv);

	/* select new IPU buf */
	ipu_idmac_select_buffer(priv->idmac_ch, priv->ipu_buf_num);
	/* toggle IPU double-buffer index */
	priv->ipu_buf_num ^= 1;

	/* bump the EOF timeout timer */
	mod_timer(&priv->eof_timeout_timer,
		  jiffies + msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT));

unlock:
	spin_unlock(&priv->irqlock);
	return IRQ_HANDLED;
}

static irqreturn_t csi_idmac_nfb4eof_interrupt(int irq, void *dev_id)
{
	struct csi_priv *priv = dev_id;

	spin_lock(&priv->irqlock);

	/*
	 * this is not an unrecoverable error, just mark
	 * the next captured frame with vb2 error flag.
	 */
	priv->nfb4eof = true;

	v4l2_err(&priv->sd, "NFB4EOF\n");

	spin_unlock(&priv->irqlock);

	return IRQ_HANDLED;
}

/*
 * EOF timeout timer function. This is an unrecoverable condition
 * without a stream restart.
 */
static void csi_idmac_eof_timeout(struct timer_list *t)
{
	struct csi_priv *priv = from_timer(priv, t, eof_timeout_timer);
	struct imx_media_video_dev *vdev = priv->vdev;

	v4l2_err(&priv->sd, "EOF timeout\n");

	/* signal a fatal error to capture device */
	imx_media_capture_device_error(vdev);
}

static void csi_idmac_setup_vb2_buf(struct csi_priv *priv, dma_addr_t *phys)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	struct imx_media_buffer *buf;
	int i;

	for (i = 0; i < 2; i++) {
		buf = imx_media_capture_device_next_buf(vdev);
		if (buf) {
			priv->active_vb2_buf[i] = buf;
			phys[i] = vb2_dma_contig_plane_dma_addr(
				&buf->vbuf.vb2_buf, 0);
		} else {
			priv->active_vb2_buf[i] = NULL;
			phys[i] = priv->underrun_buf.phys;
		}
	}
}

static void csi_idmac_unsetup_vb2_buf(struct csi_priv *priv,
				      enum vb2_buffer_state return_status)
{
	struct imx_media_buffer *buf;
	int i;

	/* return any remaining active frames with return_status */
	for (i = 0; i < 2; i++) {
		buf = priv->active_vb2_buf[i];
		if (buf) {
			struct vb2_buffer *vb = &buf->vbuf.vb2_buf;

			vb->timestamp = ktime_get_ns();
			vb2_buffer_done(vb, return_status);
		}
	}
}

/* init the SMFC IDMAC channel */
static int csi_idmac_setup_channel(struct csi_priv *priv)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	struct v4l2_mbus_framefmt *infmt;
	struct ipu_image image;
	u32 passthrough_bits;
	dma_addr_t phys[2];
	bool passthrough;
	u32 burst_size;
	int ret;

	infmt = &priv->format_mbus[CSI_SINK_PAD];

	ipu_cpmem_zero(priv->idmac_ch);

	memset(&image, 0, sizeof(image));
	image.pix = vdev->fmt.fmt.pix;
	image.rect.width = image.pix.width;
	image.rect.height = image.pix.height;

	csi_idmac_setup_vb2_buf(priv, phys);

	image.phys0 = phys[0];
	image.phys1 = phys[1];

	/*
	 * Check for conditions that require the IPU to handle the
	 * data internally as generic data, aka passthrough mode:
	 * - raw bayer formats
	 * - the CSI is receiving from a 16-bit parallel bus
	 */
	switch (image.pix.pixelformat) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_GREY:
		burst_size = 16;
		passthrough = true;
		passthrough_bits = 8;
		break;
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
	case V4L2_PIX_FMT_Y16:
		burst_size = 8;
		passthrough = true;
		passthrough_bits = 16;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12:
		burst_size = (image.pix.width & 0x3f) ?
			     ((image.pix.width & 0x1f) ?
			      ((image.pix.width & 0xf) ? 8 : 16) : 32) : 64;
		passthrough = is_parallel_16bit_bus(&priv->upstream_ep);
		passthrough_bits = 16;
		/* Skip writing U and V components to odd rows */
		ipu_cpmem_skip_odd_chroma_rows(priv->idmac_ch);
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
		burst_size = (image.pix.width & 0x1f) ?
			     ((image.pix.width & 0xf) ? 8 : 16) : 32;
		passthrough = is_parallel_16bit_bus(&priv->upstream_ep);
		passthrough_bits = 16;
		break;
	default:
		burst_size = (image.pix.width & 0xf) ? 8 : 16;
		passthrough = is_parallel_16bit_bus(&priv->upstream_ep);
		passthrough_bits = 16;
		break;
	}

	if (passthrough) {
		ipu_cpmem_set_resolution(priv->idmac_ch, image.rect.width,
					 image.rect.height);
		ipu_cpmem_set_stride(priv->idmac_ch, image.pix.bytesperline);
		ipu_cpmem_set_buffer(priv->idmac_ch, 0, image.phys0);
		ipu_cpmem_set_buffer(priv->idmac_ch, 1, image.phys1);
		ipu_cpmem_set_format_passthrough(priv->idmac_ch,
						 passthrough_bits);
	} else {
		ret = ipu_cpmem_set_image(priv->idmac_ch, &image);
		if (ret)
			goto unsetup_vb2;
	}

	ipu_cpmem_set_burstsize(priv->idmac_ch, burst_size);

	/*
	 * Set the channel for the direct CSI-->memory via SMFC
	 * use-case to very high priority, by enabling the watermark
	 * signal in the SMFC, enabling WM in the channel, and setting
	 * the channel priority to high.
	 *
	 * Refer to the i.mx6 rev. D TRM Table 36-8: Calculated priority
	 * value.
	 *
	 * The WM's are set very low by intention here to ensure that
	 * the SMFC FIFOs do not overflow.
	 */
	ipu_smfc_set_watermark(priv->smfc, 0x02, 0x01);
	ipu_cpmem_set_high_priority(priv->idmac_ch);
	ipu_idmac_enable_watermark(priv->idmac_ch, true);
	ipu_cpmem_set_axi_id(priv->idmac_ch, 0);

	burst_size = passthrough ?
		(burst_size >> 3) - 1 : (burst_size >> 2) - 1;

	ipu_smfc_set_burstsize(priv->smfc, burst_size);

	if (image.pix.field == V4L2_FIELD_NONE &&
	    V4L2_FIELD_HAS_BOTH(infmt->field))
		ipu_cpmem_interlaced_scan(priv->idmac_ch,
					  image.pix.bytesperline);

	ipu_idmac_set_double_buffer(priv->idmac_ch, true);

	return 0;

unsetup_vb2:
	csi_idmac_unsetup_vb2_buf(priv, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void csi_idmac_unsetup(struct csi_priv *priv,
			      enum vb2_buffer_state state)
{
	ipu_idmac_disable_channel(priv->idmac_ch);
	ipu_smfc_disable(priv->smfc);

	csi_idmac_unsetup_vb2_buf(priv, state);
}

static int csi_idmac_setup(struct csi_priv *priv)
{
	int ret;

	ret = csi_idmac_setup_channel(priv);
	if (ret)
		return ret;

	ipu_cpmem_dump(priv->idmac_ch);
	ipu_dump(priv->ipu);

	ipu_smfc_enable(priv->smfc);

	/* set buffers ready */
	ipu_idmac_select_buffer(priv->idmac_ch, 0);
	ipu_idmac_select_buffer(priv->idmac_ch, 1);

	/* enable the channels */
	ipu_idmac_enable_channel(priv->idmac_ch);

	return 0;
}

static int csi_idmac_start(struct csi_priv *priv)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	struct v4l2_pix_format *outfmt;
	int ret;

	ret = csi_idmac_get_ipu_resources(priv);
	if (ret)
		return ret;

	ipu_smfc_map_channel(priv->smfc, priv->csi_id, priv->vc_num);

	outfmt = &vdev->fmt.fmt.pix;

	ret = imx_media_alloc_dma_buf(priv->md, &priv->underrun_buf,
				      outfmt->sizeimage);
	if (ret)
		goto out_put_ipu;

	priv->ipu_buf_num = 0;

	/* init EOF completion waitq */
	init_completion(&priv->last_eof_comp);
	priv->last_eof = false;
	priv->nfb4eof = false;

	ret = csi_idmac_setup(priv);
	if (ret) {
		v4l2_err(&priv->sd, "csi_idmac_setup failed: %d\n", ret);
		goto out_free_dma_buf;
	}

	priv->nfb4eof_irq = ipu_idmac_channel_irq(priv->ipu,
						 priv->idmac_ch,
						 IPU_IRQ_NFB4EOF);
	ret = devm_request_irq(priv->dev, priv->nfb4eof_irq,
			       csi_idmac_nfb4eof_interrupt, 0,
			       "imx-smfc-nfb4eof", priv);
	if (ret) {
		v4l2_err(&priv->sd,
			 "Error registering NFB4EOF irq: %d\n", ret);
		goto out_unsetup;
	}

	priv->eof_irq = ipu_idmac_channel_irq(priv->ipu, priv->idmac_ch,
					      IPU_IRQ_EOF);

	ret = devm_request_irq(priv->dev, priv->eof_irq,
			       csi_idmac_eof_interrupt, 0,
			       "imx-smfc-eof", priv);
	if (ret) {
		v4l2_err(&priv->sd,
			 "Error registering eof irq: %d\n", ret);
		goto out_free_nfb4eof_irq;
	}

	/* start the EOF timeout timer */
	mod_timer(&priv->eof_timeout_timer,
		  jiffies + msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT));

	return 0;

out_free_nfb4eof_irq:
	devm_free_irq(priv->dev, priv->nfb4eof_irq, priv);
out_unsetup:
	csi_idmac_unsetup(priv, VB2_BUF_STATE_QUEUED);
out_free_dma_buf:
	imx_media_free_dma_buf(priv->md, &priv->underrun_buf);
out_put_ipu:
	csi_idmac_put_ipu_resources(priv);
	return ret;
}

static void csi_idmac_stop(struct csi_priv *priv)
{
	unsigned long flags;
	int ret;

	/* mark next EOF interrupt as the last before stream off */
	spin_lock_irqsave(&priv->irqlock, flags);
	priv->last_eof = true;
	spin_unlock_irqrestore(&priv->irqlock, flags);

	/*
	 * and then wait for interrupt handler to mark completion.
	 */
	ret = wait_for_completion_timeout(
		&priv->last_eof_comp, msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT));
	if (ret == 0)
		v4l2_warn(&priv->sd, "wait last EOF timeout\n");

	devm_free_irq(priv->dev, priv->eof_irq, priv);
	devm_free_irq(priv->dev, priv->nfb4eof_irq, priv);

	csi_idmac_unsetup(priv, VB2_BUF_STATE_ERROR);

	imx_media_free_dma_buf(priv->md, &priv->underrun_buf);

	/* cancel the EOF timeout timer */
	del_timer_sync(&priv->eof_timeout_timer);

	csi_idmac_put_ipu_resources(priv);
}

/* Update the CSI whole sensor and active windows */
static int csi_setup(struct csi_priv *priv)
{
	struct v4l2_mbus_framefmt *infmt, *outfmt;
	struct v4l2_mbus_config mbus_cfg;
	struct v4l2_mbus_framefmt if_fmt;

	infmt = &priv->format_mbus[CSI_SINK_PAD];
	outfmt = &priv->format_mbus[priv->active_output_pad];

	/* compose mbus_config from the upstream endpoint */
	mbus_cfg.type = priv->upstream_ep.bus_type;
	mbus_cfg.flags = (priv->upstream_ep.bus_type == V4L2_MBUS_CSI2) ?
		priv->upstream_ep.bus.mipi_csi2.flags :
		priv->upstream_ep.bus.parallel.flags;

	/*
	 * we need to pass input frame to CSI interface, but
	 * with translated field type from output format
	 */
	if_fmt = *infmt;
	if_fmt.field = outfmt->field;

	ipu_csi_set_window(priv->csi, &priv->crop);

	ipu_csi_set_downsize(priv->csi,
			     priv->crop.width == 2 * priv->compose.width,
			     priv->crop.height == 2 * priv->compose.height);

	ipu_csi_init_interface(priv->csi, &mbus_cfg, &if_fmt);

	ipu_csi_set_dest(priv->csi, priv->dest);

	if (priv->dest == IPU_CSI_DEST_IDMAC)
		ipu_csi_set_skip_smfc(priv->csi, priv->skip->skip_smfc,
				      priv->skip->max_ratio - 1, 0);

	ipu_csi_dump(priv->csi);

	return 0;
}

static int csi_start(struct csi_priv *priv)
{
	struct v4l2_fract *output_fi;
	int ret;

	output_fi = &priv->frame_interval[priv->active_output_pad];

	if (priv->dest == IPU_CSI_DEST_IDMAC) {
		ret = csi_idmac_start(priv);
		if (ret)
			return ret;
	}

	ret = csi_setup(priv);
	if (ret)
		goto idmac_stop;

	/* start the frame interval monitor */
	if (priv->fim && priv->dest == IPU_CSI_DEST_IDMAC) {
		ret = imx_media_fim_set_stream(priv->fim, output_fi, true);
		if (ret)
			goto idmac_stop;
	}

	ret = ipu_csi_enable(priv->csi);
	if (ret) {
		v4l2_err(&priv->sd, "CSI enable error: %d\n", ret);
		goto fim_off;
	}

	return 0;

fim_off:
	if (priv->fim && priv->dest == IPU_CSI_DEST_IDMAC)
		imx_media_fim_set_stream(priv->fim, NULL, false);
idmac_stop:
	if (priv->dest == IPU_CSI_DEST_IDMAC)
		csi_idmac_stop(priv);
	return ret;
}

static void csi_stop(struct csi_priv *priv)
{
	if (priv->dest == IPU_CSI_DEST_IDMAC) {
		csi_idmac_stop(priv);

		/* stop the frame interval monitor */
		if (priv->fim)
			imx_media_fim_set_stream(priv->fim, NULL, false);
	}

	ipu_csi_disable(priv->csi);
}

static const struct csi_skip_desc csi_skip[12] = {
	{ 1, 1, 0x00 }, /* Keep all frames */
	{ 5, 6, 0x10 }, /* Skip every sixth frame */
	{ 4, 5, 0x08 }, /* Skip every fifth frame */
	{ 3, 4, 0x04 }, /* Skip every fourth frame */
	{ 2, 3, 0x02 }, /* Skip every third frame */
	{ 3, 5, 0x0a }, /* Skip frames 1 and 3 of every 5 */
	{ 1, 2, 0x01 }, /* Skip every second frame */
	{ 2, 5, 0x0b }, /* Keep frames 1 and 4 of every 5 */
	{ 1, 3, 0x03 }, /* Keep one in three frames */
	{ 1, 4, 0x07 }, /* Keep one in four frames */
	{ 1, 5, 0x0f }, /* Keep one in five frames */
	{ 1, 6, 0x1f }, /* Keep one in six frames */
};

static void csi_apply_skip_interval(const struct csi_skip_desc *skip,
				    struct v4l2_fract *interval)
{
	unsigned int div;

	interval->numerator *= skip->max_ratio;
	interval->denominator *= skip->keep;

	/* Reduce fraction to lowest terms */
	div = gcd(interval->numerator, interval->denominator);
	if (div > 1) {
		interval->numerator /= div;
		interval->denominator /= div;
	}
}

/*
 * Find the skip pattern to produce the output frame interval closest to the
 * requested one, for the given input frame interval. Updates the output frame
 * interval to the exact value.
 */
static const struct csi_skip_desc *csi_find_best_skip(struct v4l2_fract *in,
						      struct v4l2_fract *out)
{
	const struct csi_skip_desc *skip = &csi_skip[0], *best_skip = skip;
	u32 min_err = UINT_MAX;
	u64 want_us;
	int i;

	/* Default to 1:1 ratio */
	if (out->numerator == 0 || out->denominator == 0 ||
	    in->numerator == 0 || in->denominator == 0) {
		*out = *in;
		return best_skip;
	}

	want_us = div_u64((u64)USEC_PER_SEC * out->numerator, out->denominator);

	/* Find the reduction closest to the requested time per frame */
	for (i = 0; i < ARRAY_SIZE(csi_skip); i++, skip++) {
		u64 tmp, err;

		tmp = div_u64((u64)USEC_PER_SEC * in->numerator *
			      skip->max_ratio, in->denominator * skip->keep);

		err = abs((s64)tmp - want_us);
		if (err < min_err) {
			min_err = err;
			best_skip = skip;
		}
	}

	*out = *in;
	csi_apply_skip_interval(best_skip, out);

	return best_skip;
}

/*
 * V4L2 subdev operations.
 */

static int csi_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);

	if (fi->pad >= CSI_NUM_PADS)
		return -EINVAL;

	mutex_lock(&priv->lock);

	fi->interval = priv->frame_interval[fi->pad];

	mutex_unlock(&priv->lock);

	return 0;
}

static int csi_s_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_fract *input_fi;
	int ret = 0;

	mutex_lock(&priv->lock);

	input_fi = &priv->frame_interval[CSI_SINK_PAD];

	switch (fi->pad) {
	case CSI_SINK_PAD:
		/* No limits on input frame interval */
		/* Reset output intervals and frame skipping ratio to 1:1 */
		priv->frame_interval[CSI_SRC_PAD_IDMAC] = fi->interval;
		priv->frame_interval[CSI_SRC_PAD_DIRECT] = fi->interval;
		priv->skip = &csi_skip[0];
		break;
	case CSI_SRC_PAD_IDMAC:
		/*
		 * frame interval at IDMAC output pad depends on input
		 * interval, modified by frame skipping.
		 */
		priv->skip = csi_find_best_skip(input_fi, &fi->interval);
		break;
	case CSI_SRC_PAD_DIRECT:
		/*
		 * frame interval at DIRECT output pad is same as input
		 * interval.
		 */
		fi->interval = *input_fi;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	priv->frame_interval[fi->pad] = fi->interval;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&priv->lock);

	if (!priv->src_sd || !priv->sink) {
		ret = -EPIPE;
		goto out;
	}

	/*
	 * enable/disable streaming only if stream_count is
	 * going from 0 to 1 / 1 to 0.
	 */
	if (priv->stream_count != !enable)
		goto update_count;

	if (enable) {
		/* upstream must be started first, before starting CSI */
		ret = v4l2_subdev_call(priv->src_sd, video, s_stream, 1);
		ret = (ret && ret != -ENOIOCTLCMD) ? ret : 0;
		if (ret)
			goto out;

		dev_dbg(priv->dev, "stream ON\n");
		ret = csi_start(priv);
		if (ret) {
			v4l2_subdev_call(priv->src_sd, video, s_stream, 0);
			goto out;
		}
	} else {
		dev_dbg(priv->dev, "stream OFF\n");
		/* CSI must be stopped first, then stop upstream */
		csi_stop(priv);
		v4l2_subdev_call(priv->src_sd, video, s_stream, 0);
	}

update_count:
	priv->stream_count += enable ? 1 : -1;
	if (priv->stream_count < 0)
		priv->stream_count = 0;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_subdev *remote_sd;
	int ret = 0;

	dev_dbg(priv->dev, "link setup %s -> %s\n", remote->entity->name,
		local->entity->name);

	mutex_lock(&priv->lock);

	if (local->flags & MEDIA_PAD_FL_SINK) {
		if (!is_media_entity_v4l2_subdev(remote->entity)) {
			ret = -EINVAL;
			goto out;
		}

		remote_sd = media_entity_to_v4l2_subdev(remote->entity);

		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (priv->src_sd) {
				ret = -EBUSY;
				goto out;
			}
			priv->src_sd = remote_sd;
		} else {
			priv->src_sd = NULL;
		}

		goto out;
	}

	/* this is a source pad */

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (priv->sink) {
			ret = -EBUSY;
			goto out;
		}
	} else {
		v4l2_ctrl_handler_free(&priv->ctrl_hdlr);
		v4l2_ctrl_handler_init(&priv->ctrl_hdlr, 0);
		priv->sink = NULL;
		goto out;
	}

	/* record which output pad is now active */
	priv->active_output_pad = local->index;

	/* set CSI destination */
	if (local->index == CSI_SRC_PAD_IDMAC) {
		if (!is_media_entity_v4l2_video_device(remote->entity)) {
			ret = -EINVAL;
			goto out;
		}

		if (priv->fim) {
			ret = imx_media_fim_add_controls(priv->fim);
			if (ret)
				goto out;
		}

		priv->dest = IPU_CSI_DEST_IDMAC;
	} else {
		if (!is_media_entity_v4l2_subdev(remote->entity)) {
			ret = -EINVAL;
			goto out;
		}

		remote_sd = media_entity_to_v4l2_subdev(remote->entity);
		switch (remote_sd->grp_id) {
		case IMX_MEDIA_GRP_ID_VDIC:
			priv->dest = IPU_CSI_DEST_VDIC;
			break;
		case IMX_MEDIA_GRP_ID_IC_PRP:
			priv->dest = IPU_CSI_DEST_IC;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
	}

	priv->sink = remote->entity;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_link_validate(struct v4l2_subdev *sd,
			     struct media_link *link,
			     struct v4l2_subdev_format *source_fmt,
			     struct v4l2_subdev_format *sink_fmt)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_fwnode_endpoint upstream_ep = {};
	const struct imx_media_pixfmt *incc;
	bool is_csi2;
	int ret;

	ret = v4l2_subdev_link_validate_default(sd, link,
						source_fmt, sink_fmt);
	if (ret)
		return ret;

	ret = csi_get_upstream_endpoint(priv, &upstream_ep);
	if (ret) {
		v4l2_err(&priv->sd, "failed to find upstream endpoint\n");
		return ret;
	}

	mutex_lock(&priv->lock);

	priv->upstream_ep = upstream_ep;
	is_csi2 = (upstream_ep.bus_type == V4L2_MBUS_CSI2);
	incc = priv->cc[CSI_SINK_PAD];

	if (priv->dest != IPU_CSI_DEST_IDMAC &&
	    (incc->bayer || is_parallel_16bit_bus(&upstream_ep))) {
		v4l2_err(&priv->sd,
			 "bayer/16-bit parallel buses must go to IDMAC pad\n");
		ret = -EINVAL;
		goto out;
	}

	if (is_csi2) {
		int vc_num = 0;
		/*
		 * NOTE! It seems the virtual channels from the mipi csi-2
		 * receiver are used only for routing by the video mux's,
		 * or for hard-wired routing to the CSI's. Once the stream
		 * enters the CSI's however, they are treated internally
		 * in the IPU as virtual channel 0.
		 */
#if 0
		mutex_unlock(&priv->lock);
		vc_num = imx_media_find_mipi_csi2_channel(priv->md,
							  &priv->sd.entity);
		if (vc_num < 0)
			return vc_num;
		mutex_lock(&priv->lock);
#endif
		ipu_csi_set_mipi_datatype(priv->csi, vc_num,
					  &priv->format_mbus[CSI_SINK_PAD]);
	}

	/* select either parallel or MIPI-CSI2 as input to CSI */
	ipu_set_csi_src_mux(priv->ipu, priv->csi_id, is_csi2);
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static struct v4l2_mbus_framefmt *
__csi_get_fmt(struct csi_priv *priv, struct v4l2_subdev_pad_config *cfg,
	      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&priv->sd, cfg, pad);
	else
		return &priv->format_mbus[pad];
}

static struct v4l2_rect *
__csi_get_crop(struct csi_priv *priv, struct v4l2_subdev_pad_config *cfg,
	       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&priv->sd, cfg, CSI_SINK_PAD);
	else
		return &priv->crop;
}

static struct v4l2_rect *
__csi_get_compose(struct csi_priv *priv, struct v4l2_subdev_pad_config *cfg,
		  enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_compose(&priv->sd, cfg,
						   CSI_SINK_PAD);
	else
		return &priv->compose;
}

static void csi_try_crop(struct csi_priv *priv,
			 struct v4l2_rect *crop,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_mbus_framefmt *infmt,
			 struct v4l2_fwnode_endpoint *upstream_ep)
{
	crop->width = min_t(__u32, infmt->width, crop->width);
	if (crop->left + crop->width > infmt->width)
		crop->left = infmt->width - crop->width;
	/* adjust crop left/width to h/w alignment restrictions */
	crop->left &= ~0x3;
	crop->width &= ~0x7;

	/*
	 * FIXME: not sure why yet, but on interlaced bt.656,
	 * changing the vertical cropping causes loss of vertical
	 * sync, so fix it to NTSC/PAL active lines. NTSC contains
	 * 2 extra lines of active video that need to be cropped.
	 */
	if (upstream_ep->bus_type == V4L2_MBUS_BT656 &&
	    (V4L2_FIELD_HAS_BOTH(infmt->field) ||
	     infmt->field == V4L2_FIELD_ALTERNATE)) {
		crop->height = infmt->height;
		crop->top = (infmt->height == 480) ? 2 : 0;
	} else {
		crop->height = min_t(__u32, infmt->height, crop->height);
		if (crop->top + crop->height > infmt->height)
			crop->top = infmt->height - crop->height;
	}
}

static int csi_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	const struct imx_media_pixfmt *incc;
	struct v4l2_mbus_framefmt *infmt;
	int ret = 0;

	mutex_lock(&priv->lock);

	infmt = __csi_get_fmt(priv, cfg, CSI_SINK_PAD, code->which);
	incc = imx_media_find_mbus_format(infmt->code, CS_SEL_ANY, true);

	switch (code->pad) {
	case CSI_SINK_PAD:
		ret = imx_media_enum_mbus_format(&code->code, code->index,
						 CS_SEL_ANY, true);
		break;
	case CSI_SRC_PAD_DIRECT:
	case CSI_SRC_PAD_IDMAC:
		if (incc->bayer) {
			if (code->index != 0) {
				ret = -EINVAL;
				goto out;
			}
			code->code = infmt->code;
		} else {
			u32 cs_sel = (incc->cs == IPUV3_COLORSPACE_YUV) ?
				CS_SEL_YUV : CS_SEL_RGB;
			ret = imx_media_enum_ipu_format(&code->code,
							code->index,
							cs_sel);
		}
		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_rect *crop;
	int ret = 0;

	if (fse->pad >= CSI_NUM_PADS ||
	    fse->index > (fse->pad == CSI_SINK_PAD ? 0 : 3))
		return -EINVAL;

	mutex_lock(&priv->lock);

	if (fse->pad == CSI_SINK_PAD) {
		fse->min_width = MIN_W;
		fse->max_width = MAX_W;
		fse->min_height = MIN_H;
		fse->max_height = MAX_H;
	} else {
		crop = __csi_get_crop(priv, cfg, fse->which);

		fse->min_width = fse->max_width = fse->index & 1 ?
			crop->width / 2 : crop->width;
		fse->min_height = fse->max_height = fse->index & 2 ?
			crop->height / 2 : crop->height;
	}

	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_enum_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_interval_enum *fie)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_fract *input_fi;
	struct v4l2_rect *crop;
	int ret = 0;

	if (fie->pad >= CSI_NUM_PADS ||
	    fie->index >= (fie->pad != CSI_SRC_PAD_IDMAC ?
			   1 : ARRAY_SIZE(csi_skip)))
		return -EINVAL;

	mutex_lock(&priv->lock);

	input_fi = &priv->frame_interval[CSI_SINK_PAD];
	crop = __csi_get_crop(priv, cfg, fie->which);

	if ((fie->width != crop->width && fie->width != crop->width / 2) ||
	    (fie->height != crop->height && fie->height != crop->height / 2)) {
		ret = -EINVAL;
		goto out;
	}

	fie->interval = *input_fi;

	if (fie->pad == CSI_SRC_PAD_IDMAC)
		csi_apply_skip_interval(&csi_skip[fie->index],
					&fie->interval);

out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *sdformat)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	if (sdformat->pad >= CSI_NUM_PADS)
		return -EINVAL;

	mutex_lock(&priv->lock);

	fmt = __csi_get_fmt(priv, cfg, sdformat->pad, sdformat->which);
	if (!fmt) {
		ret = -EINVAL;
		goto out;
	}

	sdformat->format = *fmt;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static void csi_try_fmt(struct csi_priv *priv,
			struct v4l2_fwnode_endpoint *upstream_ep,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *sdformat,
			struct v4l2_rect *crop,
			struct v4l2_rect *compose,
			const struct imx_media_pixfmt **cc)
{
	const struct imx_media_pixfmt *incc;
	struct v4l2_mbus_framefmt *infmt;
	u32 code;

	infmt = __csi_get_fmt(priv, cfg, CSI_SINK_PAD, sdformat->which);

	switch (sdformat->pad) {
	case CSI_SRC_PAD_DIRECT:
	case CSI_SRC_PAD_IDMAC:
		incc = imx_media_find_mbus_format(infmt->code,
						  CS_SEL_ANY, true);

		sdformat->format.width = compose->width;
		sdformat->format.height = compose->height;

		if (incc->bayer) {
			sdformat->format.code = infmt->code;
			*cc = incc;
		} else {
			u32 cs_sel = (incc->cs == IPUV3_COLORSPACE_YUV) ?
				CS_SEL_YUV : CS_SEL_RGB;

			*cc = imx_media_find_ipu_format(sdformat->format.code,
							cs_sel);
			if (!*cc) {
				imx_media_enum_ipu_format(&code, 0, cs_sel);
				*cc = imx_media_find_ipu_format(code, cs_sel);
				sdformat->format.code = (*cc)->codes[0];
			}
		}

		if (sdformat->pad == CSI_SRC_PAD_DIRECT ||
		    sdformat->format.field != V4L2_FIELD_NONE)
			sdformat->format.field = infmt->field;

		/*
		 * translate V4L2_FIELD_ALTERNATE to SEQ_TB or SEQ_BT
		 * depending on input height (assume NTSC top-bottom
		 * order if 480 lines, otherwise PAL bottom-top order).
		 */
		if (sdformat->format.field == V4L2_FIELD_ALTERNATE) {
			sdformat->format.field =  (infmt->height == 480) ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT;
		}

		/* propagate colorimetry from sink */
		sdformat->format.colorspace = infmt->colorspace;
		sdformat->format.xfer_func = infmt->xfer_func;
		sdformat->format.quantization = infmt->quantization;
		sdformat->format.ycbcr_enc = infmt->ycbcr_enc;
		break;
	case CSI_SINK_PAD:
		v4l_bound_align_image(&sdformat->format.width, MIN_W, MAX_W,
				      W_ALIGN, &sdformat->format.height,
				      MIN_H, MAX_H, H_ALIGN, S_ALIGN);

		/* Reset crop and compose rectangles */
		crop->left = 0;
		crop->top = 0;
		crop->width = sdformat->format.width;
		crop->height = sdformat->format.height;
		csi_try_crop(priv, crop, cfg, &sdformat->format, upstream_ep);
		compose->left = 0;
		compose->top = 0;
		compose->width = crop->width;
		compose->height = crop->height;

		*cc = imx_media_find_mbus_format(sdformat->format.code,
						 CS_SEL_ANY, true);
		if (!*cc) {
			imx_media_enum_mbus_format(&code, 0,
						   CS_SEL_ANY, false);
			*cc = imx_media_find_mbus_format(code,
							CS_SEL_ANY, false);
			sdformat->format.code = (*cc)->codes[0];
		}

		imx_media_fill_default_mbus_fields(
			&sdformat->format, infmt,
			priv->active_output_pad == CSI_SRC_PAD_DIRECT);
		break;
	}
}

static int csi_set_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *sdformat)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct imx_media_video_dev *vdev = priv->vdev;
	struct v4l2_fwnode_endpoint upstream_ep;
	const struct imx_media_pixfmt *cc;
	struct v4l2_pix_format vdev_fmt;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop, *compose;
	int ret;

	if (sdformat->pad >= CSI_NUM_PADS)
		return -EINVAL;

	ret = csi_get_upstream_endpoint(priv, &upstream_ep);
	if (ret) {
		v4l2_err(&priv->sd, "failed to find upstream endpoint\n");
		return ret;
	}

	mutex_lock(&priv->lock);

	if (priv->stream_count > 0) {
		ret = -EBUSY;
		goto out;
	}

	crop = __csi_get_crop(priv, cfg, sdformat->which);
	compose = __csi_get_compose(priv, cfg, sdformat->which);

	csi_try_fmt(priv, &upstream_ep, cfg, sdformat, crop, compose, &cc);

	fmt = __csi_get_fmt(priv, cfg, sdformat->pad, sdformat->which);
	*fmt = sdformat->format;

	if (sdformat->pad == CSI_SINK_PAD) {
		int pad;

		/* propagate format to source pads */
		for (pad = CSI_SINK_PAD + 1; pad < CSI_NUM_PADS; pad++) {
			const struct imx_media_pixfmt *outcc;
			struct v4l2_mbus_framefmt *outfmt;
			struct v4l2_subdev_format format;

			format.pad = pad;
			format.which = sdformat->which;
			format.format = sdformat->format;
			csi_try_fmt(priv, &upstream_ep, cfg, &format,
				    NULL, compose, &outcc);

			outfmt = __csi_get_fmt(priv, cfg, pad, sdformat->which);
			*outfmt = format.format;

			if (sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
				priv->cc[pad] = outcc;
		}
	}

	if (sdformat->which == V4L2_SUBDEV_FORMAT_TRY)
		goto out;

	priv->cc[sdformat->pad] = cc;

	/* propagate IDMAC output pad format to capture device */
	imx_media_mbus_fmt_to_pix_fmt(&vdev_fmt,
				      &priv->format_mbus[CSI_SRC_PAD_IDMAC],
				      priv->cc[CSI_SRC_PAD_IDMAC]);
	mutex_unlock(&priv->lock);
	imx_media_capture_device_set_format(vdev, &vdev_fmt);

	return 0;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *infmt;
	struct v4l2_rect *crop, *compose;
	int ret = 0;

	if (sel->pad != CSI_SINK_PAD)
		return -EINVAL;

	mutex_lock(&priv->lock);

	infmt = __csi_get_fmt(priv, cfg, CSI_SINK_PAD, sel->which);
	crop = __csi_get_crop(priv, cfg, sel->which);
	compose = __csi_get_compose(priv, cfg, sel->which);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = infmt->width;
		sel->r.height = infmt->height;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *crop;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = crop->width;
		sel->r.height = crop->height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		sel->r = *compose;
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_set_scale(u32 *compose, u32 crop, u32 flags)
{
	if ((flags & (V4L2_SEL_FLAG_LE | V4L2_SEL_FLAG_GE)) ==
		     (V4L2_SEL_FLAG_LE | V4L2_SEL_FLAG_GE) &&
	    *compose != crop && *compose != crop / 2)
		return -ERANGE;

	if (*compose <= crop / 2 ||
	    (*compose < crop * 3 / 4 && !(flags & V4L2_SEL_FLAG_GE)) ||
	    (*compose < crop && (flags & V4L2_SEL_FLAG_LE)))
		*compose = crop / 2;
	else
		*compose = crop;

	return 0;
}

static int csi_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct v4l2_fwnode_endpoint upstream_ep;
	struct v4l2_mbus_framefmt *infmt;
	struct v4l2_rect *crop, *compose;
	int pad, ret;

	if (sel->pad != CSI_SINK_PAD)
		return -EINVAL;

	ret = csi_get_upstream_endpoint(priv, &upstream_ep);
	if (ret) {
		v4l2_err(&priv->sd, "failed to find upstream endpoint\n");
		return ret;
	}

	mutex_lock(&priv->lock);

	if (priv->stream_count > 0) {
		ret = -EBUSY;
		goto out;
	}

	infmt = __csi_get_fmt(priv, cfg, CSI_SINK_PAD, sel->which);
	crop = __csi_get_crop(priv, cfg, sel->which);
	compose = __csi_get_compose(priv, cfg, sel->which);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		/*
		 * Modifying the crop rectangle always changes the format on
		 * the source pads. If the KEEP_CONFIG flag is set, just return
		 * the current crop rectangle.
		 */
		if (sel->flags & V4L2_SEL_FLAG_KEEP_CONFIG) {
			sel->r = priv->crop;
			if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
				*crop = sel->r;
			goto out;
		}

		csi_try_crop(priv, &sel->r, cfg, infmt, &upstream_ep);

		*crop = sel->r;

		/* Reset scaling to 1:1 */
		compose->width = crop->width;
		compose->height = crop->height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		/*
		 * Modifying the compose rectangle always changes the format on
		 * the source pads. If the KEEP_CONFIG flag is set, just return
		 * the current compose rectangle.
		 */
		if (sel->flags & V4L2_SEL_FLAG_KEEP_CONFIG) {
			sel->r = priv->compose;
			if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
				*compose = sel->r;
			goto out;
		}

		sel->r.left = 0;
		sel->r.top = 0;
		ret = csi_set_scale(&sel->r.width, crop->width, sel->flags);
		if (ret)
			goto out;
		ret = csi_set_scale(&sel->r.height, crop->height, sel->flags);
		if (ret)
			goto out;

		*compose = sel->r;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	/* Reset source pads to sink compose rectangle */
	for (pad = CSI_SINK_PAD + 1; pad < CSI_NUM_PADS; pad++) {
		struct v4l2_mbus_framefmt *outfmt;

		outfmt = __csi_get_fmt(priv, cfg, pad, sel->which);
		outfmt->width = compose->width;
		outfmt->height = compose->height;
	}

out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int csi_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
			       struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_IMX_FRAME_INTERVAL_ERROR)
		return -EINVAL;
	if (sub->id != 0)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static int csi_unsubscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				 struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

/*
 * retrieve our pads parsed from the OF graph by the media device
 */
static int csi_registered(struct v4l2_subdev *sd)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);
	struct ipu_csi *csi;
	int i, ret;
	u32 code;

	/* get media device */
	priv->md = dev_get_drvdata(sd->v4l2_dev->dev);

	/* get handle to IPU CSI */
	csi = ipu_csi_get(priv->ipu, priv->csi_id);
	if (IS_ERR(csi)) {
		v4l2_err(&priv->sd, "failed to get CSI%d\n", priv->csi_id);
		return PTR_ERR(csi);
	}
	priv->csi = csi;

	for (i = 0; i < CSI_NUM_PADS; i++) {
		priv->pad[i].flags = (i == CSI_SINK_PAD) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

		code = 0;
		if (i != CSI_SINK_PAD)
			imx_media_enum_ipu_format(&code, 0, CS_SEL_YUV);

		/* set a default mbus format  */
		ret = imx_media_init_mbus_fmt(&priv->format_mbus[i],
					      640, 480, code, V4L2_FIELD_NONE,
					      &priv->cc[i]);
		if (ret)
			goto put_csi;

		/* init default frame interval */
		priv->frame_interval[i].numerator = 1;
		priv->frame_interval[i].denominator = 30;
	}

	/* disable frame skipping */
	priv->skip = &csi_skip[0];

	/* init default crop and compose rectangle sizes */
	priv->crop.width = 640;
	priv->crop.height = 480;
	priv->compose.width = 640;
	priv->compose.height = 480;

	priv->fim = imx_media_fim_init(&priv->sd);
	if (IS_ERR(priv->fim)) {
		ret = PTR_ERR(priv->fim);
		goto put_csi;
	}

	ret = media_entity_pads_init(&sd->entity, CSI_NUM_PADS, priv->pad);
	if (ret)
		goto free_fim;

	ret = imx_media_capture_device_register(priv->vdev);
	if (ret)
		goto free_fim;

	ret = imx_media_add_video_device(priv->md, priv->vdev);
	if (ret)
		goto unreg;

	return 0;
unreg:
	imx_media_capture_device_unregister(priv->vdev);
free_fim:
	if (priv->fim)
		imx_media_fim_free(priv->fim);
put_csi:
	ipu_csi_put(priv->csi);
	return ret;
}

static void csi_unregistered(struct v4l2_subdev *sd)
{
	struct csi_priv *priv = v4l2_get_subdevdata(sd);

	imx_media_capture_device_unregister(priv->vdev);

	if (priv->fim)
		imx_media_fim_free(priv->fim);

	if (priv->csi)
		ipu_csi_put(priv->csi);
}

static const struct media_entity_operations csi_entity_ops = {
	.link_setup = csi_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_core_ops csi_core_ops = {
	.subscribe_event = csi_subscribe_event,
	.unsubscribe_event = csi_unsubscribe_event,
};

static const struct v4l2_subdev_video_ops csi_video_ops = {
	.g_frame_interval = csi_g_frame_interval,
	.s_frame_interval = csi_s_frame_interval,
	.s_stream = csi_s_stream,
};

static const struct v4l2_subdev_pad_ops csi_pad_ops = {
	.init_cfg = imx_media_init_cfg,
	.enum_mbus_code = csi_enum_mbus_code,
	.enum_frame_size = csi_enum_frame_size,
	.enum_frame_interval = csi_enum_frame_interval,
	.get_fmt = csi_get_fmt,
	.set_fmt = csi_set_fmt,
	.get_selection = csi_get_selection,
	.set_selection = csi_set_selection,
	.link_validate = csi_link_validate,
};

static const struct v4l2_subdev_ops csi_subdev_ops = {
	.core = &csi_core_ops,
	.video = &csi_video_ops,
	.pad = &csi_pad_ops,
};

static const struct v4l2_subdev_internal_ops csi_internal_ops = {
	.registered = csi_registered,
	.unregistered = csi_unregistered,
};

static int imx_csi_probe(struct platform_device *pdev)
{
	struct ipu_client_platformdata *pdata;
	struct pinctrl *pinctrl;
	struct csi_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, &priv->sd);
	priv->dev = &pdev->dev;

	ret = dma_set_coherent_mask(priv->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/* get parent IPU */
	priv->ipu = dev_get_drvdata(priv->dev->parent);

	/* get our CSI id */
	pdata = priv->dev->platform_data;
	priv->csi_id = pdata->csi;
	priv->smfc_id = (priv->csi_id == 0) ? 0 : 2;

	timer_setup(&priv->eof_timeout_timer, csi_idmac_eof_timeout, 0);
	spin_lock_init(&priv->irqlock);

	v4l2_subdev_init(&priv->sd, &csi_subdev_ops);
	v4l2_set_subdevdata(&priv->sd, priv);
	priv->sd.internal_ops = &csi_internal_ops;
	priv->sd.entity.ops = &csi_entity_ops;
	priv->sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	priv->sd.dev = &pdev->dev;
	priv->sd.fwnode = of_fwnode_handle(pdata->of_node);
	priv->sd.owner = THIS_MODULE;
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	priv->sd.grp_id = priv->csi_id ?
		IMX_MEDIA_GRP_ID_CSI1 : IMX_MEDIA_GRP_ID_CSI0;
	imx_media_grp_id_to_sd_name(priv->sd.name, sizeof(priv->sd.name),
				    priv->sd.grp_id, ipu_get_num(priv->ipu));

	priv->vdev = imx_media_capture_device_init(&priv->sd,
						   CSI_SRC_PAD_IDMAC);
	if (IS_ERR(priv->vdev))
		return PTR_ERR(priv->vdev);

	mutex_init(&priv->lock);

	v4l2_ctrl_handler_init(&priv->ctrl_hdlr, 0);
	priv->sd.ctrl_handler = &priv->ctrl_hdlr;

	/*
	 * The IPUv3 driver did not assign an of_node to this
	 * device. As a result, pinctrl does not automatically
	 * configure our pin groups, so we need to do that manually
	 * here, after setting this device's of_node.
	 */
	priv->dev->of_node = pdata->of_node;
	pinctrl = devm_pinctrl_get_select_default(priv->dev);
	if (IS_ERR(pinctrl)) {
		ret = PTR_ERR(priv->vdev);
		dev_dbg(priv->dev,
			"devm_pinctrl_get_select_default() failed: %d\n", ret);
		if (ret != -ENODEV)
			goto free;
	}

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto free;

	return 0;
free:
	v4l2_ctrl_handler_free(&priv->ctrl_hdlr);
	mutex_destroy(&priv->lock);
	imx_media_capture_device_remove(priv->vdev);
	return ret;
}

static int imx_csi_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct csi_priv *priv = sd_to_dev(sd);

	v4l2_ctrl_handler_free(&priv->ctrl_hdlr);
	mutex_destroy(&priv->lock);
	imx_media_capture_device_remove(priv->vdev);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct platform_device_id imx_csi_ids[] = {
	{ .name = "imx-ipuv3-csi" },
	{ },
};
MODULE_DEVICE_TABLE(platform, imx_csi_ids);

static struct platform_driver imx_csi_driver = {
	.probe = imx_csi_probe,
	.remove = imx_csi_remove,
	.id_table = imx_csi_ids,
	.driver = {
		.name = "imx-ipuv3-csi",
	},
};
module_platform_driver(imx_csi_driver);

MODULE_DESCRIPTION("i.MX CSI subdev driver");
MODULE_AUTHOR("Steve Longerbeam <steve_longerbeam@mentor.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-ipuv3-csi");
