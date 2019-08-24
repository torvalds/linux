// SPDX-License-Identifier: GPL-2.0+
/*
 * V4L2 Capture IC Preprocess Subdev for Freescale i.MX5/6 SOC
 *
 * This subdevice handles capture of video frames from the CSI or VDIC,
 * which are routed directly to the Image Converter preprocess tasks,
 * for resizing, colorspace conversion, and rotation.
 *
 * Copyright (c) 2012-2017 Mentor Graphics Inc.
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/imx.h>
#include "imx-media.h"
#include "imx-ic.h"

/*
 * Min/Max supported width and heights.
 *
 * We allow planar output, so we have to align width at the source pad
 * by 16 pixels to meet IDMAC alignment requirements for possible planar
 * output.
 *
 * TODO: move this into pad format negotiation, if capture device
 * has not requested a planar format, we should allow 8 pixel
 * alignment at the source pad.
 */
#define MIN_W_SINK  176
#define MIN_H_SINK  144
#define MAX_W_SINK 4096
#define MAX_H_SINK 4096
#define W_ALIGN_SINK  3 /* multiple of 8 pixels */
#define H_ALIGN_SINK  1 /* multiple of 2 lines */

#define MAX_W_SRC  1024
#define MAX_H_SRC  1024
#define W_ALIGN_SRC   1 /* multiple of 2 pixels */
#define H_ALIGN_SRC   1 /* multiple of 2 lines */

#define S_ALIGN       1 /* multiple of 2 */

struct prp_priv {
	struct imx_ic_priv *ic_priv;
	struct media_pad pad[PRPENCVF_NUM_PADS];
	/* the video device at output pad */
	struct imx_media_video_dev *vdev;

	/* lock to protect all members below */
	struct mutex lock;

	/* IPU units we require */
	struct ipu_ic *ic;
	struct ipuv3_channel *out_ch;
	struct ipuv3_channel *rot_in_ch;
	struct ipuv3_channel *rot_out_ch;

	/* active vb2 buffers to send to video dev sink */
	struct imx_media_buffer *active_vb2_buf[2];
	struct imx_media_dma_buf underrun_buf;

	int ipu_buf_num;  /* ipu double buffer index: 0-1 */

	/* the sink for the captured frames */
	struct media_entity *sink;
	/* the source subdev */
	struct v4l2_subdev *src_sd;

	struct v4l2_mbus_framefmt format_mbus[PRPENCVF_NUM_PADS];
	const struct imx_media_pixfmt *cc[PRPENCVF_NUM_PADS];
	struct v4l2_fract frame_interval;

	struct imx_media_dma_buf rot_buf[2];

	/* controls */
	struct v4l2_ctrl_handler ctrl_hdlr;
	int  rotation; /* degrees */
	bool hflip;
	bool vflip;

	/* derived from rotation, hflip, vflip controls */
	enum ipu_rotate_mode rot_mode;

	spinlock_t irqlock; /* protect eof_irq handler */

	struct timer_list eof_timeout_timer;
	int eof_irq;
	int nfb4eof_irq;

	int stream_count;
	u32 frame_sequence; /* frame sequence counter */
	bool last_eof;  /* waiting for last EOF at stream off */
	bool nfb4eof;    /* NFB4EOF encountered during streaming */
	bool interweave_swap; /* swap top/bottom lines when interweaving */
	struct completion last_eof_comp;
};

static const struct prp_channels {
	u32 out_ch;
	u32 rot_in_ch;
	u32 rot_out_ch;
} prp_channel[] = {
	[IC_TASK_ENCODER] = {
		.out_ch = IPUV3_CHANNEL_IC_PRP_ENC_MEM,
		.rot_in_ch = IPUV3_CHANNEL_MEM_ROT_ENC,
		.rot_out_ch = IPUV3_CHANNEL_ROT_ENC_MEM,
	},
	[IC_TASK_VIEWFINDER] = {
		.out_ch = IPUV3_CHANNEL_IC_PRP_VF_MEM,
		.rot_in_ch = IPUV3_CHANNEL_MEM_ROT_VF,
		.rot_out_ch = IPUV3_CHANNEL_ROT_VF_MEM,
	},
};

static inline struct prp_priv *sd_to_priv(struct v4l2_subdev *sd)
{
	struct imx_ic_priv *ic_priv = v4l2_get_subdevdata(sd);

	return ic_priv->task_priv;
}

static void prp_put_ipu_resources(struct prp_priv *priv)
{
	if (priv->ic)
		ipu_ic_put(priv->ic);
	priv->ic = NULL;

	if (priv->out_ch)
		ipu_idmac_put(priv->out_ch);
	priv->out_ch = NULL;

	if (priv->rot_in_ch)
		ipu_idmac_put(priv->rot_in_ch);
	priv->rot_in_ch = NULL;

	if (priv->rot_out_ch)
		ipu_idmac_put(priv->rot_out_ch);
	priv->rot_out_ch = NULL;
}

static int prp_get_ipu_resources(struct prp_priv *priv)
{
	struct imx_ic_priv *ic_priv = priv->ic_priv;
	struct ipu_ic *ic;
	struct ipuv3_channel *out_ch, *rot_in_ch, *rot_out_ch;
	int ret, task = ic_priv->task_id;

	ic = ipu_ic_get(ic_priv->ipu, task);
	if (IS_ERR(ic)) {
		v4l2_err(&ic_priv->sd, "failed to get IC\n");
		ret = PTR_ERR(ic);
		goto out;
	}
	priv->ic = ic;

	out_ch = ipu_idmac_get(ic_priv->ipu, prp_channel[task].out_ch);
	if (IS_ERR(out_ch)) {
		v4l2_err(&ic_priv->sd, "could not get IDMAC channel %u\n",
			 prp_channel[task].out_ch);
		ret = PTR_ERR(out_ch);
		goto out;
	}
	priv->out_ch = out_ch;

	rot_in_ch = ipu_idmac_get(ic_priv->ipu, prp_channel[task].rot_in_ch);
	if (IS_ERR(rot_in_ch)) {
		v4l2_err(&ic_priv->sd, "could not get IDMAC channel %u\n",
			 prp_channel[task].rot_in_ch);
		ret = PTR_ERR(rot_in_ch);
		goto out;
	}
	priv->rot_in_ch = rot_in_ch;

	rot_out_ch = ipu_idmac_get(ic_priv->ipu, prp_channel[task].rot_out_ch);
	if (IS_ERR(rot_out_ch)) {
		v4l2_err(&ic_priv->sd, "could not get IDMAC channel %u\n",
			 prp_channel[task].rot_out_ch);
		ret = PTR_ERR(rot_out_ch);
		goto out;
	}
	priv->rot_out_ch = rot_out_ch;

	return 0;
out:
	prp_put_ipu_resources(priv);
	return ret;
}

static void prp_vb2_buf_done(struct prp_priv *priv, struct ipuv3_channel *ch)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	struct imx_media_buffer *done, *next;
	struct vb2_buffer *vb;
	dma_addr_t phys;

	done = priv->active_vb2_buf[priv->ipu_buf_num];
	if (done) {
		done->vbuf.field = vdev->fmt.fmt.pix.field;
		done->vbuf.sequence = priv->frame_sequence;
		vb = &done->vbuf.vb2_buf;
		vb->timestamp = ktime_get_ns();
		vb2_buffer_done(vb, priv->nfb4eof ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	}

	priv->frame_sequence++;
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

	if (ipu_idmac_buffer_is_ready(ch, priv->ipu_buf_num))
		ipu_idmac_clear_buffer(ch, priv->ipu_buf_num);

	if (priv->interweave_swap && ch == priv->out_ch)
		phys += vdev->fmt.fmt.pix.bytesperline;

	ipu_cpmem_set_buffer(ch, priv->ipu_buf_num, phys);
}

static irqreturn_t prp_eof_interrupt(int irq, void *dev_id)
{
	struct prp_priv *priv = dev_id;
	struct ipuv3_channel *channel;

	spin_lock(&priv->irqlock);

	if (priv->last_eof) {
		complete(&priv->last_eof_comp);
		priv->last_eof = false;
		goto unlock;
	}

	channel = (ipu_rot_mode_is_irt(priv->rot_mode)) ?
		priv->rot_out_ch : priv->out_ch;

	prp_vb2_buf_done(priv, channel);

	/* select new IPU buf */
	ipu_idmac_select_buffer(channel, priv->ipu_buf_num);
	/* toggle IPU double-buffer index */
	priv->ipu_buf_num ^= 1;

	/* bump the EOF timeout timer */
	mod_timer(&priv->eof_timeout_timer,
		  jiffies + msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT));

unlock:
	spin_unlock(&priv->irqlock);
	return IRQ_HANDLED;
}

static irqreturn_t prp_nfb4eof_interrupt(int irq, void *dev_id)
{
	struct prp_priv *priv = dev_id;
	struct imx_ic_priv *ic_priv = priv->ic_priv;

	spin_lock(&priv->irqlock);

	/*
	 * this is not an unrecoverable error, just mark
	 * the next captured frame with vb2 error flag.
	 */
	priv->nfb4eof = true;

	v4l2_err(&ic_priv->sd, "NFB4EOF\n");

	spin_unlock(&priv->irqlock);

	return IRQ_HANDLED;
}

/*
 * EOF timeout timer function.
 */
/*
 * EOF timeout timer function. This is an unrecoverable condition
 * without a stream restart.
 */
static void prp_eof_timeout(struct timer_list *t)
{
	struct prp_priv *priv = from_timer(priv, t, eof_timeout_timer);
	struct imx_media_video_dev *vdev = priv->vdev;
	struct imx_ic_priv *ic_priv = priv->ic_priv;

	v4l2_err(&ic_priv->sd, "EOF timeout\n");

	/* signal a fatal error to capture device */
	imx_media_capture_device_error(vdev);
}

static void prp_setup_vb2_buf(struct prp_priv *priv, dma_addr_t *phys)
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

static void prp_unsetup_vb2_buf(struct prp_priv *priv,
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

static int prp_setup_channel(struct prp_priv *priv,
			     struct ipuv3_channel *channel,
			     enum ipu_rotate_mode rot_mode,
			     dma_addr_t addr0, dma_addr_t addr1,
			     bool rot_swap_width_height)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	const struct imx_media_pixfmt *outcc;
	struct v4l2_mbus_framefmt *outfmt;
	unsigned int burst_size;
	struct ipu_image image;
	bool interweave;
	int ret;

	outfmt = &priv->format_mbus[PRPENCVF_SRC_PAD];
	outcc = vdev->cc;

	ipu_cpmem_zero(channel);

	memset(&image, 0, sizeof(image));
	image.pix = vdev->fmt.fmt.pix;
	image.rect = vdev->compose;

	/*
	 * If the field type at capture interface is interlaced, and
	 * the output IDMAC pad is sequential, enable interweave at
	 * the IDMAC output channel.
	 */
	interweave = V4L2_FIELD_IS_INTERLACED(image.pix.field) &&
		V4L2_FIELD_IS_SEQUENTIAL(outfmt->field);
	priv->interweave_swap = interweave &&
		image.pix.field == V4L2_FIELD_INTERLACED_BT;

	if (rot_swap_width_height) {
		swap(image.pix.width, image.pix.height);
		swap(image.rect.width, image.rect.height);
		/* recalc stride using swapped width */
		image.pix.bytesperline = outcc->planar ?
			image.pix.width :
			(image.pix.width * outcc->bpp) >> 3;
	}

	if (priv->interweave_swap && channel == priv->out_ch) {
		/* start interweave scan at 1st top line (2nd line) */
		image.rect.top = 1;
	}

	image.phys0 = addr0;
	image.phys1 = addr1;

	/*
	 * Skip writing U and V components to odd rows in the output
	 * channels for planar 4:2:0 (but not when enabling IDMAC
	 * interweaving, they are incompatible).
	 */
	if ((channel == priv->out_ch && !interweave) ||
	    channel == priv->rot_out_ch) {
		switch (image.pix.pixelformat) {
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YVU420:
		case V4L2_PIX_FMT_NV12:
			ipu_cpmem_skip_odd_chroma_rows(channel);
			break;
		}
	}

	ret = ipu_cpmem_set_image(channel, &image);
	if (ret)
		return ret;

	if (channel == priv->rot_in_ch ||
	    channel == priv->rot_out_ch) {
		burst_size = 8;
		ipu_cpmem_set_block_mode(channel);
	} else {
		burst_size = (image.pix.width & 0xf) ? 8 : 16;
	}

	ipu_cpmem_set_burstsize(channel, burst_size);

	if (rot_mode)
		ipu_cpmem_set_rotation(channel, rot_mode);

	if (interweave && channel == priv->out_ch)
		ipu_cpmem_interlaced_scan(channel,
					  priv->interweave_swap ?
					  -image.pix.bytesperline :
					  image.pix.bytesperline,
					  image.pix.pixelformat);

	ret = ipu_ic_task_idma_init(priv->ic, channel,
				    image.pix.width, image.pix.height,
				    burst_size, rot_mode);
	if (ret)
		return ret;

	ipu_cpmem_set_axi_id(channel, 1);

	ipu_idmac_set_double_buffer(channel, true);

	return 0;
}

static int prp_setup_rotation(struct prp_priv *priv)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	struct imx_ic_priv *ic_priv = priv->ic_priv;
	const struct imx_media_pixfmt *outcc, *incc;
	struct v4l2_mbus_framefmt *infmt;
	struct v4l2_pix_format *outfmt;
	struct ipu_ic_csc csc;
	dma_addr_t phys[2];
	int ret;

	infmt = &priv->format_mbus[PRPENCVF_SINK_PAD];
	outfmt = &vdev->fmt.fmt.pix;
	incc = priv->cc[PRPENCVF_SINK_PAD];
	outcc = vdev->cc;

	ret = ipu_ic_calc_csc(&csc,
			      infmt->ycbcr_enc, infmt->quantization,
			      incc->cs,
			      outfmt->ycbcr_enc, outfmt->quantization,
			      outcc->cs);
	if (ret) {
		v4l2_err(&ic_priv->sd, "ipu_ic_calc_csc failed, %d\n",
			 ret);
		return ret;
	}

	ret = imx_media_alloc_dma_buf(ic_priv->ipu_dev, &priv->rot_buf[0],
				      outfmt->sizeimage);
	if (ret) {
		v4l2_err(&ic_priv->sd, "failed to alloc rot_buf[0], %d\n", ret);
		return ret;
	}
	ret = imx_media_alloc_dma_buf(ic_priv->ipu_dev, &priv->rot_buf[1],
				      outfmt->sizeimage);
	if (ret) {
		v4l2_err(&ic_priv->sd, "failed to alloc rot_buf[1], %d\n", ret);
		goto free_rot0;
	}

	ret = ipu_ic_task_init(priv->ic, &csc,
			       infmt->width, infmt->height,
			       outfmt->height, outfmt->width);
	if (ret) {
		v4l2_err(&ic_priv->sd, "ipu_ic_task_init failed, %d\n", ret);
		goto free_rot1;
	}

	/* init the IC-PRP-->MEM IDMAC channel */
	ret = prp_setup_channel(priv, priv->out_ch, IPU_ROTATE_NONE,
				priv->rot_buf[0].phys, priv->rot_buf[1].phys,
				true);
	if (ret) {
		v4l2_err(&ic_priv->sd,
			 "prp_setup_channel(out_ch) failed, %d\n", ret);
		goto free_rot1;
	}

	/* init the MEM-->IC-PRP ROT IDMAC channel */
	ret = prp_setup_channel(priv, priv->rot_in_ch, priv->rot_mode,
				priv->rot_buf[0].phys, priv->rot_buf[1].phys,
				true);
	if (ret) {
		v4l2_err(&ic_priv->sd,
			 "prp_setup_channel(rot_in_ch) failed, %d\n", ret);
		goto free_rot1;
	}

	prp_setup_vb2_buf(priv, phys);

	/* init the destination IC-PRP ROT-->MEM IDMAC channel */
	ret = prp_setup_channel(priv, priv->rot_out_ch, IPU_ROTATE_NONE,
				phys[0], phys[1],
				false);
	if (ret) {
		v4l2_err(&ic_priv->sd,
			 "prp_setup_channel(rot_out_ch) failed, %d\n", ret);
		goto unsetup_vb2;
	}

	/* now link IC-PRP-->MEM to MEM-->IC-PRP ROT */
	ipu_idmac_link(priv->out_ch, priv->rot_in_ch);

	/* enable the IC */
	ipu_ic_enable(priv->ic);

	/* set buffers ready */
	ipu_idmac_select_buffer(priv->out_ch, 0);
	ipu_idmac_select_buffer(priv->out_ch, 1);
	ipu_idmac_select_buffer(priv->rot_out_ch, 0);
	ipu_idmac_select_buffer(priv->rot_out_ch, 1);

	/* enable the channels */
	ipu_idmac_enable_channel(priv->out_ch);
	ipu_idmac_enable_channel(priv->rot_in_ch);
	ipu_idmac_enable_channel(priv->rot_out_ch);

	/* and finally enable the IC PRP task */
	ipu_ic_task_enable(priv->ic);

	return 0;

unsetup_vb2:
	prp_unsetup_vb2_buf(priv, VB2_BUF_STATE_QUEUED);
free_rot1:
	imx_media_free_dma_buf(ic_priv->ipu_dev, &priv->rot_buf[1]);
free_rot0:
	imx_media_free_dma_buf(ic_priv->ipu_dev, &priv->rot_buf[0]);
	return ret;
}

static void prp_unsetup_rotation(struct prp_priv *priv)
{
	struct imx_ic_priv *ic_priv = priv->ic_priv;

	ipu_ic_task_disable(priv->ic);

	ipu_idmac_disable_channel(priv->out_ch);
	ipu_idmac_disable_channel(priv->rot_in_ch);
	ipu_idmac_disable_channel(priv->rot_out_ch);

	ipu_idmac_unlink(priv->out_ch, priv->rot_in_ch);

	ipu_ic_disable(priv->ic);

	imx_media_free_dma_buf(ic_priv->ipu_dev, &priv->rot_buf[0]);
	imx_media_free_dma_buf(ic_priv->ipu_dev, &priv->rot_buf[1]);
}

static int prp_setup_norotation(struct prp_priv *priv)
{
	struct imx_media_video_dev *vdev = priv->vdev;
	struct imx_ic_priv *ic_priv = priv->ic_priv;
	const struct imx_media_pixfmt *outcc, *incc;
	struct v4l2_mbus_framefmt *infmt;
	struct v4l2_pix_format *outfmt;
	struct ipu_ic_csc csc;
	dma_addr_t phys[2];
	int ret;

	infmt = &priv->format_mbus[PRPENCVF_SINK_PAD];
	outfmt = &vdev->fmt.fmt.pix;
	incc = priv->cc[PRPENCVF_SINK_PAD];
	outcc = vdev->cc;

	ret = ipu_ic_calc_csc(&csc,
			      infmt->ycbcr_enc, infmt->quantization,
			      incc->cs,
			      outfmt->ycbcr_enc, outfmt->quantization,
			      outcc->cs);
	if (ret) {
		v4l2_err(&ic_priv->sd, "ipu_ic_calc_csc failed, %d\n",
			 ret);
		return ret;
	}

	ret = ipu_ic_task_init(priv->ic, &csc,
			       infmt->width, infmt->height,
			       outfmt->width, outfmt->height);
	if (ret) {
		v4l2_err(&ic_priv->sd, "ipu_ic_task_init failed, %d\n", ret);
		return ret;
	}

	prp_setup_vb2_buf(priv, phys);

	/* init the IC PRP-->MEM IDMAC channel */
	ret = prp_setup_channel(priv, priv->out_ch, priv->rot_mode,
				phys[0], phys[1], false);
	if (ret) {
		v4l2_err(&ic_priv->sd,
			 "prp_setup_channel(out_ch) failed, %d\n", ret);
		goto unsetup_vb2;
	}

	ipu_cpmem_dump(priv->out_ch);
	ipu_ic_dump(priv->ic);
	ipu_dump(ic_priv->ipu);

	ipu_ic_enable(priv->ic);

	/* set buffers ready */
	ipu_idmac_select_buffer(priv->out_ch, 0);
	ipu_idmac_select_buffer(priv->out_ch, 1);

	/* enable the channels */
	ipu_idmac_enable_channel(priv->out_ch);

	/* enable the IC task */
	ipu_ic_task_enable(priv->ic);

	return 0;

unsetup_vb2:
	prp_unsetup_vb2_buf(priv, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void prp_unsetup_norotation(struct prp_priv *priv)
{
	ipu_ic_task_disable(priv->ic);
	ipu_idmac_disable_channel(priv->out_ch);
	ipu_ic_disable(priv->ic);
}

static void prp_unsetup(struct prp_priv *priv,
			enum vb2_buffer_state state)
{
	if (ipu_rot_mode_is_irt(priv->rot_mode))
		prp_unsetup_rotation(priv);
	else
		prp_unsetup_norotation(priv);

	prp_unsetup_vb2_buf(priv, state);
}

static int prp_start(struct prp_priv *priv)
{
	struct imx_ic_priv *ic_priv = priv->ic_priv;
	struct imx_media_video_dev *vdev = priv->vdev;
	struct v4l2_pix_format *outfmt;
	int ret;

	ret = prp_get_ipu_resources(priv);
	if (ret)
		return ret;

	outfmt = &vdev->fmt.fmt.pix;

	ret = imx_media_alloc_dma_buf(ic_priv->ipu_dev, &priv->underrun_buf,
				      outfmt->sizeimage);
	if (ret)
		goto out_put_ipu;

	priv->ipu_buf_num = 0;

	/* init EOF completion waitq */
	init_completion(&priv->last_eof_comp);
	priv->frame_sequence = 0;
	priv->last_eof = false;
	priv->nfb4eof = false;

	if (ipu_rot_mode_is_irt(priv->rot_mode))
		ret = prp_setup_rotation(priv);
	else
		ret = prp_setup_norotation(priv);
	if (ret)
		goto out_free_underrun;

	priv->nfb4eof_irq = ipu_idmac_channel_irq(ic_priv->ipu,
						  priv->out_ch,
						  IPU_IRQ_NFB4EOF);
	ret = devm_request_irq(ic_priv->ipu_dev, priv->nfb4eof_irq,
			       prp_nfb4eof_interrupt, 0,
			       "imx-ic-prp-nfb4eof", priv);
	if (ret) {
		v4l2_err(&ic_priv->sd,
			 "Error registering NFB4EOF irq: %d\n", ret);
		goto out_unsetup;
	}

	if (ipu_rot_mode_is_irt(priv->rot_mode))
		priv->eof_irq = ipu_idmac_channel_irq(
			ic_priv->ipu, priv->rot_out_ch, IPU_IRQ_EOF);
	else
		priv->eof_irq = ipu_idmac_channel_irq(
			ic_priv->ipu, priv->out_ch, IPU_IRQ_EOF);

	ret = devm_request_irq(ic_priv->ipu_dev, priv->eof_irq,
			       prp_eof_interrupt, 0,
			       "imx-ic-prp-eof", priv);
	if (ret) {
		v4l2_err(&ic_priv->sd,
			 "Error registering eof irq: %d\n", ret);
		goto out_free_nfb4eof_irq;
	}

	/* start upstream */
	ret = v4l2_subdev_call(priv->src_sd, video, s_stream, 1);
	ret = (ret && ret != -ENOIOCTLCMD) ? ret : 0;
	if (ret) {
		v4l2_err(&ic_priv->sd,
			 "upstream stream on failed: %d\n", ret);
		goto out_free_eof_irq;
	}

	/* start the EOF timeout timer */
	mod_timer(&priv->eof_timeout_timer,
		  jiffies + msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT));

	return 0;

out_free_eof_irq:
	devm_free_irq(ic_priv->ipu_dev, priv->eof_irq, priv);
out_free_nfb4eof_irq:
	devm_free_irq(ic_priv->ipu_dev, priv->nfb4eof_irq, priv);
out_unsetup:
	prp_unsetup(priv, VB2_BUF_STATE_QUEUED);
out_free_underrun:
	imx_media_free_dma_buf(ic_priv->ipu_dev, &priv->underrun_buf);
out_put_ipu:
	prp_put_ipu_resources(priv);
	return ret;
}

static void prp_stop(struct prp_priv *priv)
{
	struct imx_ic_priv *ic_priv = priv->ic_priv;
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
		&priv->last_eof_comp,
		msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT));
	if (ret == 0)
		v4l2_warn(&ic_priv->sd, "wait last EOF timeout\n");

	/* stop upstream */
	ret = v4l2_subdev_call(priv->src_sd, video, s_stream, 0);
	if (ret && ret != -ENOIOCTLCMD)
		v4l2_warn(&ic_priv->sd,
			  "upstream stream off failed: %d\n", ret);

	devm_free_irq(ic_priv->ipu_dev, priv->eof_irq, priv);
	devm_free_irq(ic_priv->ipu_dev, priv->nfb4eof_irq, priv);

	prp_unsetup(priv, VB2_BUF_STATE_ERROR);

	imx_media_free_dma_buf(ic_priv->ipu_dev, &priv->underrun_buf);

	/* cancel the EOF timeout timer */
	del_timer_sync(&priv->eof_timeout_timer);

	prp_put_ipu_resources(priv);
}

static struct v4l2_mbus_framefmt *
__prp_get_fmt(struct prp_priv *priv, struct v4l2_subdev_pad_config *cfg,
	      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	struct imx_ic_priv *ic_priv = priv->ic_priv;

	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&ic_priv->sd, cfg, pad);
	else
		return &priv->format_mbus[pad];
}

/*
 * Applies IC resizer and IDMAC alignment restrictions to output
 * rectangle given the input rectangle, and depending on given
 * rotation mode.
 *
 * The IC resizer cannot downsize more than 4:1. Note also that
 * for 90 or 270 rotation, _both_ output width and height must
 * be aligned by W_ALIGN_SRC, because the intermediate rotation
 * buffer swaps output width/height, and the final output buffer
 * does not.
 *
 * Returns true if the output rectangle was modified.
 */
static bool prp_bound_align_output(struct v4l2_mbus_framefmt *outfmt,
				   struct v4l2_mbus_framefmt *infmt,
				   enum ipu_rotate_mode rot_mode)
{
	u32 orig_width = outfmt->width;
	u32 orig_height = outfmt->height;

	if (ipu_rot_mode_is_irt(rot_mode))
		v4l_bound_align_image(&outfmt->width,
				      infmt->height / 4, MAX_H_SRC,
				      W_ALIGN_SRC,
				      &outfmt->height,
				      infmt->width / 4, MAX_W_SRC,
				      W_ALIGN_SRC, S_ALIGN);
	else
		v4l_bound_align_image(&outfmt->width,
				      infmt->width / 4, MAX_W_SRC,
				      W_ALIGN_SRC,
				      &outfmt->height,
				      infmt->height / 4, MAX_H_SRC,
				      H_ALIGN_SRC, S_ALIGN);

	return outfmt->width != orig_width || outfmt->height != orig_height;
}

/*
 * V4L2 subdev operations.
 */

static int prp_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad >= PRPENCVF_NUM_PADS)
		return -EINVAL;

	return imx_media_enum_ipu_format(&code->code, code->index, CS_SEL_ANY);
}

static int prp_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *sdformat)
{
	struct prp_priv *priv = sd_to_priv(sd);
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	if (sdformat->pad >= PRPENCVF_NUM_PADS)
		return -EINVAL;

	mutex_lock(&priv->lock);

	fmt = __prp_get_fmt(priv, cfg, sdformat->pad, sdformat->which);
	if (!fmt) {
		ret = -EINVAL;
		goto out;
	}

	sdformat->format = *fmt;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static void prp_try_fmt(struct prp_priv *priv,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *sdformat,
			const struct imx_media_pixfmt **cc)
{
	struct v4l2_mbus_framefmt *infmt;

	*cc = imx_media_find_ipu_format(sdformat->format.code, CS_SEL_ANY);
	if (!*cc) {
		u32 code;

		imx_media_enum_ipu_format(&code, 0, CS_SEL_ANY);
		*cc = imx_media_find_ipu_format(code, CS_SEL_ANY);
		sdformat->format.code = (*cc)->codes[0];
	}

	infmt = __prp_get_fmt(priv, cfg, PRPENCVF_SINK_PAD, sdformat->which);

	if (sdformat->pad == PRPENCVF_SRC_PAD) {
		sdformat->format.field = infmt->field;

		prp_bound_align_output(&sdformat->format, infmt,
				       priv->rot_mode);

		/* propagate colorimetry from sink */
		sdformat->format.colorspace = infmt->colorspace;
		sdformat->format.xfer_func = infmt->xfer_func;
	} else {
		v4l_bound_align_image(&sdformat->format.width,
				      MIN_W_SINK, MAX_W_SINK, W_ALIGN_SINK,
				      &sdformat->format.height,
				      MIN_H_SINK, MAX_H_SINK, H_ALIGN_SINK,
				      S_ALIGN);

		if (sdformat->format.field == V4L2_FIELD_ANY)
			sdformat->format.field = V4L2_FIELD_NONE;
	}

	imx_media_try_colorimetry(&sdformat->format, true);
}

static int prp_set_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *sdformat)
{
	struct prp_priv *priv = sd_to_priv(sd);
	const struct imx_media_pixfmt *cc;
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	if (sdformat->pad >= PRPENCVF_NUM_PADS)
		return -EINVAL;

	mutex_lock(&priv->lock);

	if (priv->stream_count > 0) {
		ret = -EBUSY;
		goto out;
	}

	prp_try_fmt(priv, cfg, sdformat, &cc);

	fmt = __prp_get_fmt(priv, cfg, sdformat->pad, sdformat->which);
	*fmt = sdformat->format;

	/* propagate a default format to source pad */
	if (sdformat->pad == PRPENCVF_SINK_PAD) {
		const struct imx_media_pixfmt *outcc;
		struct v4l2_mbus_framefmt *outfmt;
		struct v4l2_subdev_format format;

		format.pad = PRPENCVF_SRC_PAD;
		format.which = sdformat->which;
		format.format = sdformat->format;
		prp_try_fmt(priv, cfg, &format, &outcc);

		outfmt = __prp_get_fmt(priv, cfg, PRPENCVF_SRC_PAD,
				       sdformat->which);
		*outfmt = format.format;
		if (sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			priv->cc[PRPENCVF_SRC_PAD] = outcc;
	}

	if (sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		priv->cc[sdformat->pad] = cc;

out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int prp_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct prp_priv *priv = sd_to_priv(sd);
	struct v4l2_subdev_format format = {};
	const struct imx_media_pixfmt *cc;
	int ret = 0;

	if (fse->pad >= PRPENCVF_NUM_PADS || fse->index != 0)
		return -EINVAL;

	mutex_lock(&priv->lock);

	format.pad = fse->pad;
	format.which = fse->which;
	format.format.code = fse->code;
	format.format.width = 1;
	format.format.height = 1;
	prp_try_fmt(priv, cfg, &format, &cc);
	fse->min_width = format.format.width;
	fse->min_height = format.format.height;

	if (format.format.code != fse->code) {
		ret = -EINVAL;
		goto out;
	}

	format.format.code = fse->code;
	format.format.width = -1;
	format.format.height = -1;
	prp_try_fmt(priv, cfg, &format, &cc);
	fse->max_width = format.format.width;
	fse->max_height = format.format.height;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int prp_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct imx_ic_priv *ic_priv = v4l2_get_subdevdata(sd);
	struct prp_priv *priv = ic_priv->task_priv;
	struct v4l2_subdev *remote_sd;
	int ret = 0;

	dev_dbg(ic_priv->ipu_dev, "%s: link setup %s -> %s",
		ic_priv->sd.name, remote->entity->name, local->entity->name);

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

	/* this is the source pad */

	/* the remote must be the device node */
	if (!is_media_entity_v4l2_video_device(remote->entity)) {
		ret = -EINVAL;
		goto out;
	}

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (priv->sink) {
			ret = -EBUSY;
			goto out;
		}
	} else {
		priv->sink = NULL;
		goto out;
	}

	priv->sink = remote->entity;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int prp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct prp_priv *priv = container_of(ctrl->handler,
					       struct prp_priv, ctrl_hdlr);
	struct imx_ic_priv *ic_priv = priv->ic_priv;
	enum ipu_rotate_mode rot_mode;
	int rotation, ret = 0;
	bool hflip, vflip;

	mutex_lock(&priv->lock);

	rotation = priv->rotation;
	hflip = priv->hflip;
	vflip = priv->vflip;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		hflip = (ctrl->val == 1);
		break;
	case V4L2_CID_VFLIP:
		vflip = (ctrl->val == 1);
		break;
	case V4L2_CID_ROTATE:
		rotation = ctrl->val;
		break;
	default:
		v4l2_err(&ic_priv->sd, "Invalid control\n");
		ret = -EINVAL;
		goto out;
	}

	ret = ipu_degrees_to_rot_mode(&rot_mode, rotation, hflip, vflip);
	if (ret)
		goto out;

	if (rot_mode != priv->rot_mode) {
		struct v4l2_mbus_framefmt outfmt, infmt;

		/* can't change rotation mid-streaming */
		if (priv->stream_count > 0) {
			ret = -EBUSY;
			goto out;
		}

		outfmt = priv->format_mbus[PRPENCVF_SRC_PAD];
		infmt = priv->format_mbus[PRPENCVF_SINK_PAD];

		if (prp_bound_align_output(&outfmt, &infmt, rot_mode)) {
			ret = -EINVAL;
			goto out;
		}

		priv->rot_mode = rot_mode;
		priv->rotation = rotation;
		priv->hflip = hflip;
		priv->vflip = vflip;
	}

out:
	mutex_unlock(&priv->lock);
	return ret;
}

static const struct v4l2_ctrl_ops prp_ctrl_ops = {
	.s_ctrl = prp_s_ctrl,
};

static int prp_init_controls(struct prp_priv *priv)
{
	struct imx_ic_priv *ic_priv = priv->ic_priv;
	struct v4l2_ctrl_handler *hdlr = &priv->ctrl_hdlr;
	int ret;

	v4l2_ctrl_handler_init(hdlr, 3);

	v4l2_ctrl_new_std(hdlr, &prp_ctrl_ops, V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(hdlr, &prp_ctrl_ops, V4L2_CID_VFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(hdlr, &prp_ctrl_ops, V4L2_CID_ROTATE,
			  0, 270, 90, 0);

	ic_priv->sd.ctrl_handler = hdlr;

	if (hdlr->error) {
		ret = hdlr->error;
		goto out_free;
	}

	v4l2_ctrl_handler_setup(hdlr);
	return 0;

out_free:
	v4l2_ctrl_handler_free(hdlr);
	return ret;
}

static int prp_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx_ic_priv *ic_priv = v4l2_get_subdevdata(sd);
	struct prp_priv *priv = ic_priv->task_priv;
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

	dev_dbg(ic_priv->ipu_dev, "%s: stream %s\n", sd->name,
		enable ? "ON" : "OFF");

	if (enable)
		ret = prp_start(priv);
	else
		prp_stop(priv);
	if (ret)
		goto out;

update_count:
	priv->stream_count += enable ? 1 : -1;
	if (priv->stream_count < 0)
		priv->stream_count = 0;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int prp_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct prp_priv *priv = sd_to_priv(sd);

	if (fi->pad >= PRPENCVF_NUM_PADS)
		return -EINVAL;

	mutex_lock(&priv->lock);
	fi->interval = priv->frame_interval;
	mutex_unlock(&priv->lock);

	return 0;
}

static int prp_s_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct prp_priv *priv = sd_to_priv(sd);

	if (fi->pad >= PRPENCVF_NUM_PADS)
		return -EINVAL;

	mutex_lock(&priv->lock);

	/* No limits on valid frame intervals */
	if (fi->interval.numerator == 0 || fi->interval.denominator == 0)
		fi->interval = priv->frame_interval;
	else
		priv->frame_interval = fi->interval;

	mutex_unlock(&priv->lock);

	return 0;
}

static int prp_registered(struct v4l2_subdev *sd)
{
	struct prp_priv *priv = sd_to_priv(sd);
	struct imx_ic_priv *ic_priv = priv->ic_priv;
	int i, ret;
	u32 code;

	/* set a default mbus format  */
	imx_media_enum_ipu_format(&code, 0, CS_SEL_YUV);
	for (i = 0; i < PRPENCVF_NUM_PADS; i++) {
		ret = imx_media_init_mbus_fmt(&priv->format_mbus[i],
					      640, 480, code, V4L2_FIELD_NONE,
					      &priv->cc[i]);
		if (ret)
			return ret;
	}

	/* init default frame interval */
	priv->frame_interval.numerator = 1;
	priv->frame_interval.denominator = 30;

	priv->vdev = imx_media_capture_device_init(ic_priv->ipu_dev,
						   &ic_priv->sd,
						   PRPENCVF_SRC_PAD);
	if (IS_ERR(priv->vdev))
		return PTR_ERR(priv->vdev);

	ret = imx_media_capture_device_register(priv->vdev);
	if (ret)
		goto remove_vdev;

	ret = prp_init_controls(priv);
	if (ret)
		goto unreg_vdev;

	return 0;

unreg_vdev:
	imx_media_capture_device_unregister(priv->vdev);
remove_vdev:
	imx_media_capture_device_remove(priv->vdev);
	return ret;
}

static void prp_unregistered(struct v4l2_subdev *sd)
{
	struct prp_priv *priv = sd_to_priv(sd);

	imx_media_capture_device_unregister(priv->vdev);
	imx_media_capture_device_remove(priv->vdev);

	v4l2_ctrl_handler_free(&priv->ctrl_hdlr);
}

static const struct v4l2_subdev_pad_ops prp_pad_ops = {
	.init_cfg = imx_media_init_cfg,
	.enum_mbus_code = prp_enum_mbus_code,
	.enum_frame_size = prp_enum_frame_size,
	.get_fmt = prp_get_fmt,
	.set_fmt = prp_set_fmt,
};

static const struct v4l2_subdev_video_ops prp_video_ops = {
	.g_frame_interval = prp_g_frame_interval,
	.s_frame_interval = prp_s_frame_interval,
	.s_stream = prp_s_stream,
};

static const struct media_entity_operations prp_entity_ops = {
	.link_setup = prp_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_ops prp_subdev_ops = {
	.video = &prp_video_ops,
	.pad = &prp_pad_ops,
};

static const struct v4l2_subdev_internal_ops prp_internal_ops = {
	.registered = prp_registered,
	.unregistered = prp_unregistered,
};

static int prp_init(struct imx_ic_priv *ic_priv)
{
	struct prp_priv *priv;
	int i, ret;

	priv = devm_kzalloc(ic_priv->ipu_dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ic_priv->task_priv = priv;
	priv->ic_priv = ic_priv;

	spin_lock_init(&priv->irqlock);
	timer_setup(&priv->eof_timeout_timer, prp_eof_timeout, 0);

	mutex_init(&priv->lock);

	for (i = 0; i < PRPENCVF_NUM_PADS; i++) {
		priv->pad[i].flags = (i == PRPENCVF_SINK_PAD) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;
	}

	ret = media_entity_pads_init(&ic_priv->sd.entity, PRPENCVF_NUM_PADS,
				     priv->pad);
	if (ret)
		mutex_destroy(&priv->lock);

	return ret;
}

static void prp_remove(struct imx_ic_priv *ic_priv)
{
	struct prp_priv *priv = ic_priv->task_priv;

	mutex_destroy(&priv->lock);
}

struct imx_ic_ops imx_ic_prpencvf_ops = {
	.subdev_ops = &prp_subdev_ops,
	.internal_ops = &prp_internal_ops,
	.entity_ops = &prp_entity_ops,
	.init = prp_init,
	.remove = prp_remove,
};
