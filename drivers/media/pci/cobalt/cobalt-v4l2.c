/*
 *  cobalt V4L2 API
 *
 *  Derived from ivtv-ioctl.c and cx18-fileops.c
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 *
 *  This program is free software; you may redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/math64.h>
#include <linux/pci.h>
#include <linux/v4l2-dv-timings.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dv-timings.h>
#include <media/i2c/adv7604.h>
#include <media/i2c/adv7842.h>

#include "cobalt-alsa.h"
#include "cobalt-cpld.h"
#include "cobalt-driver.h"
#include "cobalt-v4l2.h"
#include "cobalt-irq.h"
#include "cobalt-omnitek.h"

static const struct v4l2_dv_timings cea1080p60 = V4L2_DV_BT_CEA_1920X1080P60;

/* vb2 DMA streaming ops */

static int cobalt_queue_setup(struct vb2_queue *q, const void *parg,
			unsigned int *num_buffers, unsigned int *num_planes,
			unsigned int sizes[], void *alloc_ctxs[])
{
	const struct v4l2_format *fmt = parg;
	struct cobalt_stream *s = q->drv_priv;
	unsigned size = s->stride * s->height;

	if (*num_buffers < 3)
		*num_buffers = 3;
	if (*num_buffers > NR_BUFS)
		*num_buffers = NR_BUFS;
	*num_planes = 1;
	if (fmt) {
		if (fmt->fmt.pix.sizeimage < size)
			return -EINVAL;
		size = fmt->fmt.pix.sizeimage;
	}
	sizes[0] = size;
	alloc_ctxs[0] = s->cobalt->alloc_ctx;
	return 0;
}

static int cobalt_buf_init(struct vb2_buffer *vb)
{
	struct cobalt_stream *s = vb->vb2_queue->drv_priv;
	struct cobalt *cobalt = s->cobalt;
	const size_t max_pages_per_line =
		(COBALT_MAX_WIDTH * COBALT_MAX_BPP) / PAGE_SIZE + 2;
	const size_t bytes =
		COBALT_MAX_HEIGHT * max_pages_per_line * 0x20;
	const size_t audio_bytes = ((1920 * 4) / PAGE_SIZE + 1) * 0x20;
	struct sg_dma_desc_info *desc = &s->dma_desc_info[vb->index];
	struct sg_table *sg_desc = vb2_dma_sg_plane_desc(vb, 0);
	unsigned size;
	int ret;

	size = s->stride * s->height;
	if (vb2_plane_size(vb, 0) < size) {
		cobalt_info("data will not fit into plane (%lu < %u)\n",
					vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	if (desc->virt == NULL) {
		desc->dev = &cobalt->pci_dev->dev;
		descriptor_list_allocate(desc,
			s->is_audio ? audio_bytes : bytes);
		if (desc->virt == NULL)
			return -ENOMEM;
	}
	ret = descriptor_list_create(cobalt, sg_desc->sgl,
			!s->is_output, sg_desc->nents, size,
			s->width * s->bpp, s->stride, desc);
	if (ret)
		descriptor_list_free(desc);
	return ret;
}

static void cobalt_buf_cleanup(struct vb2_buffer *vb)
{
	struct cobalt_stream *s = vb->vb2_queue->drv_priv;
	struct sg_dma_desc_info *desc = &s->dma_desc_info[vb->index];

	descriptor_list_free(desc);
}

static int cobalt_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cobalt_stream *s = vb->vb2_queue->drv_priv;

	vb2_set_plane_payload(vb, 0, s->stride * s->height);
	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static void chain_all_buffers(struct cobalt_stream *s)
{
	struct sg_dma_desc_info *desc[NR_BUFS];
	struct cobalt_buffer *cb;
	struct list_head *p;
	int i = 0;

	list_for_each(p, &s->bufs) {
		cb = list_entry(p, struct cobalt_buffer, list);
		desc[i] = &s->dma_desc_info[cb->vb.vb2_buf.index];
		if (i > 0)
			descriptor_list_chain(desc[i-1], desc[i]);
		i++;
	}
}

static void cobalt_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *q = vb->vb2_queue;
	struct cobalt_stream *s = q->drv_priv;
	struct cobalt_buffer *cb = to_cobalt_buffer(vbuf);
	struct sg_dma_desc_info *desc = &s->dma_desc_info[vb->index];
	unsigned long flags;

	/* Prepare new buffer */
	descriptor_list_loopback(desc);
	descriptor_list_interrupt_disable(desc);

	spin_lock_irqsave(&s->irqlock, flags);
	list_add_tail(&cb->list, &s->bufs);
	chain_all_buffers(s);
	spin_unlock_irqrestore(&s->irqlock, flags);
}

static void cobalt_enable_output(struct cobalt_stream *s)
{
	struct cobalt *cobalt = s->cobalt;
	struct v4l2_bt_timings *bt = &s->timings.bt;
	struct m00514_syncgen_flow_evcnt_regmap __iomem *vo =
		COBALT_TX_BASE(cobalt);
	unsigned fmt = s->pixfmt != V4L2_PIX_FMT_BGR32 ?
			M00514_CONTROL_BITMAP_FORMAT_16_BPP_MSK : 0;
	struct v4l2_subdev_format sd_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	if (!cobalt_cpld_set_freq(cobalt, bt->pixelclock)) {
		cobalt_err("pixelclock out of range\n");
		return;
	}

	sd_fmt.format.colorspace = s->colorspace;
	sd_fmt.format.xfer_func = s->xfer_func;
	sd_fmt.format.ycbcr_enc = s->ycbcr_enc;
	sd_fmt.format.quantization = s->quantization;
	sd_fmt.format.width = bt->width;
	sd_fmt.format.height = bt->height;

	/* Set up FDMA packer */
	switch (s->pixfmt) {
	case V4L2_PIX_FMT_YUYV:
		sd_fmt.format.code = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	case V4L2_PIX_FMT_BGR32:
		sd_fmt.format.code = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	}
	v4l2_subdev_call(s->sd, pad, set_fmt, NULL, &sd_fmt);

	iowrite32(0, &vo->control);
	/* 1080p60 */
	iowrite32(bt->hsync, &vo->sync_generator_h_sync_length);
	iowrite32(bt->hbackporch, &vo->sync_generator_h_backporch_length);
	iowrite32(bt->width, &vo->sync_generator_h_active_length);
	iowrite32(bt->hfrontporch, &vo->sync_generator_h_frontporch_length);
	iowrite32(bt->vsync, &vo->sync_generator_v_sync_length);
	iowrite32(bt->vbackporch, &vo->sync_generator_v_backporch_length);
	iowrite32(bt->height, &vo->sync_generator_v_active_length);
	iowrite32(bt->vfrontporch, &vo->sync_generator_v_frontporch_length);
	iowrite32(0x9900c1, &vo->error_color);

	iowrite32(M00514_CONTROL_BITMAP_SYNC_GENERATOR_LOAD_PARAM_MSK | fmt,
		  &vo->control);
	iowrite32(M00514_CONTROL_BITMAP_EVCNT_CLEAR_MSK | fmt, &vo->control);
	iowrite32(M00514_CONTROL_BITMAP_SYNC_GENERATOR_ENABLE_MSK |
		  M00514_CONTROL_BITMAP_FLOW_CTRL_OUTPUT_ENABLE_MSK |
		  fmt, &vo->control);
}

static void cobalt_enable_input(struct cobalt_stream *s)
{
	struct cobalt *cobalt = s->cobalt;
	int ch = (int)s->video_channel;
	struct m00235_fdma_packer_regmap __iomem *packer;
	struct v4l2_subdev_format sd_fmt_yuyv = {
		.pad = s->pad_source,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.format.code = MEDIA_BUS_FMT_YUYV8_1X16,
	};
	struct v4l2_subdev_format sd_fmt_rgb = {
		.pad = s->pad_source,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.format.code = MEDIA_BUS_FMT_RGB888_1X24,
	};

	cobalt_dbg(1, "video_channel %d (%s, %s)\n",
		   s->video_channel,
		   s->input == 0 ? "hdmi" : "generator",
		   "YUYV");

	packer = COBALT_CVI_PACKER(cobalt, ch);

	/* Set up FDMA packer */
	switch (s->pixfmt) {
	case V4L2_PIX_FMT_YUYV:
		iowrite32(M00235_CONTROL_BITMAP_ENABLE_MSK |
			  (1 << M00235_CONTROL_BITMAP_PACK_FORMAT_OFST),
			  &packer->control);
		v4l2_subdev_call(s->sd, pad, set_fmt, NULL,
				 &sd_fmt_yuyv);
		break;
	case V4L2_PIX_FMT_RGB24:
		iowrite32(M00235_CONTROL_BITMAP_ENABLE_MSK |
			  (2 << M00235_CONTROL_BITMAP_PACK_FORMAT_OFST),
			  &packer->control);
		v4l2_subdev_call(s->sd, pad, set_fmt, NULL,
				 &sd_fmt_rgb);
		break;
	case V4L2_PIX_FMT_BGR32:
		iowrite32(M00235_CONTROL_BITMAP_ENABLE_MSK |
			  M00235_CONTROL_BITMAP_ENDIAN_FORMAT_MSK |
			  (3 << M00235_CONTROL_BITMAP_PACK_FORMAT_OFST),
			  &packer->control);
		v4l2_subdev_call(s->sd, pad, set_fmt, NULL,
				 &sd_fmt_rgb);
		break;
	}
}

static void cobalt_dma_start_streaming(struct cobalt_stream *s)
{
	struct cobalt *cobalt = s->cobalt;
	int rx = s->video_channel;
	struct m00460_evcnt_regmap __iomem *evcnt =
		COBALT_CVI_EVCNT(cobalt, rx);
	struct cobalt_buffer *cb;
	unsigned long flags;

	spin_lock_irqsave(&s->irqlock, flags);
	if (!s->is_output) {
		iowrite32(M00460_CONTROL_BITMAP_CLEAR_MSK, &evcnt->control);
		iowrite32(M00460_CONTROL_BITMAP_ENABLE_MSK, &evcnt->control);
	} else {
		struct m00514_syncgen_flow_evcnt_regmap __iomem *vo =
			COBALT_TX_BASE(cobalt);
		u32 ctrl = ioread32(&vo->control);

		ctrl &= ~(M00514_CONTROL_BITMAP_EVCNT_ENABLE_MSK |
			  M00514_CONTROL_BITMAP_EVCNT_CLEAR_MSK);
		iowrite32(ctrl | M00514_CONTROL_BITMAP_EVCNT_CLEAR_MSK,
			  &vo->control);
		iowrite32(ctrl | M00514_CONTROL_BITMAP_EVCNT_ENABLE_MSK,
			  &vo->control);
	}
	cb = list_first_entry(&s->bufs, struct cobalt_buffer, list);
	omni_sg_dma_start(s, &s->dma_desc_info[cb->vb.vb2_buf.index]);
	spin_unlock_irqrestore(&s->irqlock, flags);
}

static int cobalt_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct cobalt_stream *s = q->drv_priv;
	struct cobalt *cobalt = s->cobalt;
	struct m00233_video_measure_regmap __iomem *vmr;
	struct m00473_freewheel_regmap __iomem *fw;
	struct m00479_clk_loss_detector_regmap __iomem *clkloss;
	int rx = s->video_channel;
	struct m00389_cvi_regmap __iomem *cvi = COBALT_CVI(cobalt, rx);
	struct m00460_evcnt_regmap __iomem *evcnt = COBALT_CVI_EVCNT(cobalt, rx);
	struct v4l2_bt_timings *bt = &s->timings.bt;
	u64 tot_size;
	u32 clk_freq;

	if (s->is_audio)
		goto done;
	if (s->is_output) {
		s->unstable_frame = false;
		cobalt_enable_output(s);
		goto done;
	}

	cobalt_enable_input(s);

	fw = COBALT_CVI_FREEWHEEL(cobalt, rx);
	vmr = COBALT_CVI_VMR(cobalt, rx);
	clkloss = COBALT_CVI_CLK_LOSS(cobalt, rx);

	iowrite32(M00460_CONTROL_BITMAP_CLEAR_MSK, &evcnt->control);
	iowrite32(M00460_CONTROL_BITMAP_ENABLE_MSK, &evcnt->control);
	iowrite32(bt->width, &cvi->frame_width);
	iowrite32(bt->height, &cvi->frame_height);
	tot_size = V4L2_DV_BT_FRAME_WIDTH(bt) * V4L2_DV_BT_FRAME_HEIGHT(bt);
	iowrite32(div_u64((u64)V4L2_DV_BT_FRAME_WIDTH(bt) * COBALT_CLK * 4,
			  bt->pixelclock), &vmr->hsync_timeout_val);
	iowrite32(M00233_CONTROL_BITMAP_ENABLE_MEASURE_MSK, &vmr->control);
	clk_freq = ioread32(&fw->clk_freq);
	iowrite32(clk_freq / 1000000, &clkloss->ref_clk_cnt_val);
	/* The lower bound for the clock frequency is 0.5% lower as is
	 * allowed by the spec */
	iowrite32(div_u64(bt->pixelclock * 995, 1000000000),
		  &clkloss->test_clk_cnt_val);
	/* will be enabled after the first frame has been received */
	iowrite32(bt->width * bt->height, &fw->active_length);
	iowrite32(div_u64((u64)clk_freq * tot_size, bt->pixelclock),
		  &fw->total_length);
	iowrite32(M00233_IRQ_TRIGGERS_BITMAP_VACTIVE_AREA_MSK |
		  M00233_IRQ_TRIGGERS_BITMAP_HACTIVE_AREA_MSK,
		  &vmr->irq_triggers);
	iowrite32(0, &cvi->control);
	iowrite32(M00233_CONTROL_BITMAP_ENABLE_MEASURE_MSK, &vmr->control);

	iowrite32(0xff, &fw->output_color);
	iowrite32(M00479_CTRL_BITMAP_ENABLE_MSK, &clkloss->ctrl);
	iowrite32(M00473_CTRL_BITMAP_ENABLE_MSK |
		  M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_MSK, &fw->ctrl);
	s->unstable_frame = true;
	s->enable_freewheel = false;
	s->enable_cvi = false;
	s->skip_first_frames = 0;

done:
	s->sequence = 0;
	cobalt_dma_start_streaming(s);
	return 0;
}

static void cobalt_dma_stop_streaming(struct cobalt_stream *s)
{
	struct cobalt *cobalt = s->cobalt;
	struct sg_dma_desc_info *desc;
	struct cobalt_buffer *cb;
	struct list_head *p;
	unsigned long flags;
	int timeout_msec = 100;
	int rx = s->video_channel;
	struct m00460_evcnt_regmap __iomem *evcnt =
		COBALT_CVI_EVCNT(cobalt, rx);

	if (!s->is_output) {
		iowrite32(0, &evcnt->control);
	} else if (!s->is_audio) {
		struct m00514_syncgen_flow_evcnt_regmap __iomem *vo =
			COBALT_TX_BASE(cobalt);

		iowrite32(M00514_CONTROL_BITMAP_EVCNT_CLEAR_MSK, &vo->control);
		iowrite32(0, &vo->control);
	}

	/* Try to stop the DMA engine gracefully */
	spin_lock_irqsave(&s->irqlock, flags);
	list_for_each(p, &s->bufs) {
		cb = list_entry(p, struct cobalt_buffer, list);
		desc = &s->dma_desc_info[cb->vb.vb2_buf.index];
		/* Stop DMA after this descriptor chain */
		descriptor_list_end_of_chain(desc);
	}
	spin_unlock_irqrestore(&s->irqlock, flags);

	/* Wait 100 milisecond for DMA to finish, abort on timeout. */
	if (!wait_event_timeout(s->q.done_wq, is_dma_done(s),
				msecs_to_jiffies(timeout_msec))) {
		omni_sg_dma_abort_channel(s);
		pr_warn("aborted\n");
	}
	cobalt_write_bar0(cobalt, DMA_INTERRUPT_STATUS_REG,
			1 << s->dma_channel);
}

static void cobalt_stop_streaming(struct vb2_queue *q)
{
	struct cobalt_stream *s = q->drv_priv;
	struct cobalt *cobalt = s->cobalt;
	int rx = s->video_channel;
	struct m00233_video_measure_regmap __iomem *vmr;
	struct m00473_freewheel_regmap __iomem *fw;
	struct m00479_clk_loss_detector_regmap __iomem *clkloss;
	struct cobalt_buffer *cb;
	struct list_head *p, *safe;
	unsigned long flags;

	cobalt_dma_stop_streaming(s);

	/* Return all buffers to user space */
	spin_lock_irqsave(&s->irqlock, flags);
	list_for_each_safe(p, safe, &s->bufs) {
		cb = list_entry(p, struct cobalt_buffer, list);
		list_del(&cb->list);
		vb2_buffer_done(&cb->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&s->irqlock, flags);

	if (s->is_audio || s->is_output)
		return;

	fw = COBALT_CVI_FREEWHEEL(cobalt, rx);
	vmr = COBALT_CVI_VMR(cobalt, rx);
	clkloss = COBALT_CVI_CLK_LOSS(cobalt, rx);
	iowrite32(0, &vmr->control);
	iowrite32(M00233_CONTROL_BITMAP_ENABLE_MEASURE_MSK, &vmr->control);
	iowrite32(0, &fw->ctrl);
	iowrite32(0, &clkloss->ctrl);
}

static const struct vb2_ops cobalt_qops = {
	.queue_setup = cobalt_queue_setup,
	.buf_init = cobalt_buf_init,
	.buf_cleanup = cobalt_buf_cleanup,
	.buf_prepare = cobalt_buf_prepare,
	.buf_queue = cobalt_buf_queue,
	.start_streaming = cobalt_start_streaming,
	.stop_streaming = cobalt_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

/* V4L2 ioctls */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int cobalt_cobaltc(struct cobalt *cobalt, unsigned int cmd, void *arg)
{
	struct v4l2_dbg_register *regs = arg;
	void __iomem *adrs = cobalt->bar1 + regs->reg;

	cobalt_info("cobalt_cobaltc: adrs = %p\n", adrs);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	regs->size = 4;
	if (cmd == VIDIOC_DBG_S_REGISTER)
		iowrite32(regs->val, adrs);
	else
		regs->val = ioread32(adrs);
	return 0;
}

static int cobalt_g_register(struct file *file, void *priv_fh,
		struct v4l2_dbg_register *reg)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct cobalt *cobalt = s->cobalt;

	return cobalt_cobaltc(cobalt, VIDIOC_DBG_G_REGISTER, reg);
}

static int cobalt_s_register(struct file *file, void *priv_fh,
		const struct v4l2_dbg_register *reg)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct cobalt *cobalt = s->cobalt;

	return cobalt_cobaltc(cobalt, VIDIOC_DBG_S_REGISTER,
			(struct v4l2_dbg_register *)reg);
}
#endif

static int cobalt_querycap(struct file *file, void *priv_fh,
				struct v4l2_capability *vcap)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct cobalt *cobalt = s->cobalt;

	strlcpy(vcap->driver, "cobalt", sizeof(vcap->driver));
	strlcpy(vcap->card, "cobalt", sizeof(vcap->card));
	snprintf(vcap->bus_info, sizeof(vcap->bus_info),
		 "PCIe:%s", pci_name(cobalt->pci_dev));
	vcap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	if (s->is_output)
		vcap->device_caps |= V4L2_CAP_VIDEO_OUTPUT;
	else
		vcap->device_caps |= V4L2_CAP_VIDEO_CAPTURE;
	vcap->capabilities = vcap->device_caps | V4L2_CAP_DEVICE_CAPS |
		V4L2_CAP_VIDEO_CAPTURE;
	if (cobalt->have_hsma_tx)
		vcap->capabilities |= V4L2_CAP_VIDEO_OUTPUT;
	return 0;
}

static void cobalt_video_input_status_show(struct cobalt_stream *s)
{
	struct m00389_cvi_regmap __iomem *cvi;
	struct m00233_video_measure_regmap __iomem *vmr;
	struct m00473_freewheel_regmap __iomem *fw;
	struct m00479_clk_loss_detector_regmap __iomem *clkloss;
	struct m00235_fdma_packer_regmap __iomem *packer;
	int rx = s->video_channel;
	struct cobalt *cobalt = s->cobalt;
	u32 cvi_ctrl, cvi_stat;
	u32 vmr_ctrl, vmr_stat;

	cvi = COBALT_CVI(cobalt, rx);
	vmr = COBALT_CVI_VMR(cobalt, rx);
	fw = COBALT_CVI_FREEWHEEL(cobalt, rx);
	clkloss = COBALT_CVI_CLK_LOSS(cobalt, rx);
	packer = COBALT_CVI_PACKER(cobalt, rx);
	cvi_ctrl = ioread32(&cvi->control);
	cvi_stat = ioread32(&cvi->status);
	vmr_ctrl = ioread32(&vmr->control);
	vmr_stat = ioread32(&vmr->control);
	cobalt_info("rx%d: cvi resolution: %dx%d\n", rx,
		    ioread32(&cvi->frame_width), ioread32(&cvi->frame_height));
	cobalt_info("rx%d: cvi control: %s%s%s\n", rx,
		(cvi_ctrl & M00389_CONTROL_BITMAP_ENABLE_MSK) ?
			"enable " : "disable ",
		(cvi_ctrl & M00389_CONTROL_BITMAP_HSYNC_POLARITY_LOW_MSK) ?
			"HSync- " : "HSync+ ",
		(cvi_ctrl & M00389_CONTROL_BITMAP_VSYNC_POLARITY_LOW_MSK) ?
			"VSync- " : "VSync+ ");
	cobalt_info("rx%d: cvi status: %s%s\n", rx,
		(cvi_stat & M00389_STATUS_BITMAP_LOCK_MSK) ?
			"lock " : "no-lock ",
		(cvi_stat & M00389_STATUS_BITMAP_ERROR_MSK) ?
			"error " : "no-error ");

	cobalt_info("rx%d: Measurements: %s%s%s%s%s%s%s\n", rx,
		(vmr_ctrl & M00233_CONTROL_BITMAP_HSYNC_POLARITY_LOW_MSK) ?
			"HSync- " : "HSync+ ",
		(vmr_ctrl & M00233_CONTROL_BITMAP_VSYNC_POLARITY_LOW_MSK) ?
			"VSync- " : "VSync+ ",
		(vmr_ctrl & M00233_CONTROL_BITMAP_ENABLE_MEASURE_MSK) ?
			"enabled " : "disabled ",
		(vmr_ctrl & M00233_CONTROL_BITMAP_ENABLE_INTERRUPT_MSK) ?
			"irq-enabled " : "irq-disabled ",
		(vmr_ctrl & M00233_CONTROL_BITMAP_UPDATE_ON_HSYNC_MSK) ?
			"update-on-hsync " : "",
		(vmr_stat & M00233_STATUS_BITMAP_HSYNC_TIMEOUT_MSK) ?
			"hsync-timeout " : "",
		(vmr_stat & M00233_STATUS_BITMAP_INIT_DONE_MSK) ?
			"init-done" : "");
	cobalt_info("rx%d: irq_status: 0x%02x irq_triggers: 0x%02x\n", rx,
			ioread32(&vmr->irq_status) & 0xff,
			ioread32(&vmr->irq_triggers) & 0xff);
	cobalt_info("rx%d: vsync: %d\n", rx, ioread32(&vmr->vsync_time));
	cobalt_info("rx%d: vbp: %d\n", rx, ioread32(&vmr->vback_porch));
	cobalt_info("rx%d: vact: %d\n", rx, ioread32(&vmr->vactive_area));
	cobalt_info("rx%d: vfb: %d\n", rx, ioread32(&vmr->vfront_porch));
	cobalt_info("rx%d: hsync: %d\n", rx, ioread32(&vmr->hsync_time));
	cobalt_info("rx%d: hbp: %d\n", rx, ioread32(&vmr->hback_porch));
	cobalt_info("rx%d: hact: %d\n", rx, ioread32(&vmr->hactive_area));
	cobalt_info("rx%d: hfb: %d\n", rx, ioread32(&vmr->hfront_porch));
	cobalt_info("rx%d: Freewheeling: %s%s%s\n", rx,
		(ioread32(&fw->ctrl) & M00473_CTRL_BITMAP_ENABLE_MSK) ?
			"enabled " : "disabled ",
		(ioread32(&fw->ctrl) & M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_MSK) ?
			"forced " : "",
		(ioread32(&fw->status) & M00473_STATUS_BITMAP_FREEWHEEL_MODE_MSK) ?
			"freewheeling " : "video-passthrough ");
	iowrite32(0xff, &vmr->irq_status);
	cobalt_info("rx%d: Clock Loss Detection: %s%s\n", rx,
		(ioread32(&clkloss->ctrl) & M00479_CTRL_BITMAP_ENABLE_MSK) ?
			"enabled " : "disabled ",
		(ioread32(&clkloss->status) & M00479_STATUS_BITMAP_CLOCK_MISSING_MSK) ?
			"clock-missing " : "found-clock ");
	cobalt_info("rx%d: Packer: %x\n", rx, ioread32(&packer->control));
}

static int cobalt_log_status(struct file *file, void *priv_fh)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct cobalt *cobalt = s->cobalt;
	struct m00514_syncgen_flow_evcnt_regmap __iomem *vo =
		COBALT_TX_BASE(cobalt);
	u8 stat;

	cobalt_info("%s", cobalt->hdl_info);
	cobalt_info("sysctrl: %08x, sysstat: %08x\n",
			cobalt_g_sysctrl(cobalt),
			cobalt_g_sysstat(cobalt));
	cobalt_info("dma channel: %d, video channel: %d\n",
			s->dma_channel, s->video_channel);
	cobalt_pcie_status_show(cobalt);
	cobalt_cpld_status(cobalt);
	cobalt_irq_log_status(cobalt);
	v4l2_subdev_call(s->sd, core, log_status);
	if (!s->is_output) {
		cobalt_video_input_status_show(s);
		return 0;
	}

	stat = ioread32(&vo->rd_status);

	cobalt_info("tx: status: %s%s\n",
		(stat & M00514_RD_STATUS_BITMAP_FLOW_CTRL_NO_DATA_ERROR_MSK) ?
			"no_data " : "",
		(stat & M00514_RD_STATUS_BITMAP_READY_BUFFER_FULL_MSK) ?
			"ready_buffer_full " : "");
	cobalt_info("tx: evcnt: %d\n", ioread32(&vo->rd_evcnt_count));
	return 0;
}

static int cobalt_enum_dv_timings(struct file *file, void *priv_fh,
				    struct v4l2_enum_dv_timings *timings)
{
	struct cobalt_stream *s = video_drvdata(file);

	if (s->input == 1) {
		if (timings->index)
			return -EINVAL;
		memset(timings->reserved, 0, sizeof(timings->reserved));
		timings->timings = cea1080p60;
		return 0;
	}
	timings->pad = 0;
	return v4l2_subdev_call(s->sd,
			pad, enum_dv_timings, timings);
}

static int cobalt_s_dv_timings(struct file *file, void *priv_fh,
				    struct v4l2_dv_timings *timings)
{
	struct cobalt_stream *s = video_drvdata(file);
	int err;

	if (s->input == 1) {
		*timings = cea1080p60;
		return 0;
	}

	if (v4l2_match_dv_timings(timings, &s->timings, 0, false))
		return 0;

	if (vb2_is_busy(&s->q))
		return -EBUSY;

	err = v4l2_subdev_call(s->sd,
			video, s_dv_timings, timings);
	if (!err) {
		s->timings = *timings;
		s->width = timings->bt.width;
		s->height = timings->bt.height;
		s->stride = timings->bt.width * s->bpp;
	}
	return err;
}

static int cobalt_g_dv_timings(struct file *file, void *priv_fh,
				    struct v4l2_dv_timings *timings)
{
	struct cobalt_stream *s = video_drvdata(file);

	if (s->input == 1) {
		*timings = cea1080p60;
		return 0;
	}
	return v4l2_subdev_call(s->sd,
			video, g_dv_timings, timings);
}

static int cobalt_query_dv_timings(struct file *file, void *priv_fh,
				    struct v4l2_dv_timings *timings)
{
	struct cobalt_stream *s = video_drvdata(file);

	if (s->input == 1) {
		*timings = cea1080p60;
		return 0;
	}
	return v4l2_subdev_call(s->sd,
			video, query_dv_timings, timings);
}

static int cobalt_dv_timings_cap(struct file *file, void *priv_fh,
				    struct v4l2_dv_timings_cap *cap)
{
	struct cobalt_stream *s = video_drvdata(file);

	cap->pad = 0;
	return v4l2_subdev_call(s->sd,
			pad, dv_timings_cap, cap);
}

static int cobalt_enum_fmt_vid_cap(struct file *file, void *priv_fh,
		struct v4l2_fmtdesc *f)
{
	switch (f->index) {
	case 0:
		strlcpy(f->description, "YUV 4:2:2", sizeof(f->description));
		f->pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	case 1:
		strlcpy(f->description, "RGB24", sizeof(f->description));
		f->pixelformat = V4L2_PIX_FMT_RGB24;
		break;
	case 2:
		strlcpy(f->description, "RGB32", sizeof(f->description));
		f->pixelformat = V4L2_PIX_FMT_BGR32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cobalt_g_fmt_vid_cap(struct file *file, void *priv_fh,
		struct v4l2_format *f)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev_format sd_fmt;

	pix->width = s->width;
	pix->height = s->height;
	pix->bytesperline = s->stride;
	pix->field = V4L2_FIELD_NONE;

	if (s->input == 1) {
		pix->colorspace = V4L2_COLORSPACE_SRGB;
	} else {
		sd_fmt.pad = s->pad_source;
		sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		v4l2_subdev_call(s->sd, pad, get_fmt, NULL, &sd_fmt);
		v4l2_fill_pix_format(pix, &sd_fmt.format);
	}

	pix->pixelformat = s->pixfmt;
	pix->sizeimage = pix->bytesperline * pix->height;

	return 0;
}

static int cobalt_try_fmt_vid_cap(struct file *file, void *priv_fh,
		struct v4l2_format *f)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev_format sd_fmt;

	/* Check for min (QCIF) and max (Full HD) size */
	if ((pix->width < 176) || (pix->height < 144)) {
		pix->width = 176;
		pix->height = 144;
	}

	if ((pix->width > 1920) || (pix->height > 1080)) {
		pix->width = 1920;
		pix->height = 1080;
	}

	/* Make width multiple of 4 */
	pix->width &= ~0x3;

	/* Make height multiple of 2 */
	pix->height &= ~0x1;

	if (s->input == 1) {
		/* Generator => fixed format only */
		pix->width = 1920;
		pix->height = 1080;
		pix->colorspace = V4L2_COLORSPACE_SRGB;
	} else {
		sd_fmt.pad = s->pad_source;
		sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		v4l2_subdev_call(s->sd, pad, get_fmt, NULL, &sd_fmt);
		v4l2_fill_pix_format(pix, &sd_fmt.format);
	}

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	default:
		pix->bytesperline = max(pix->bytesperline & ~0x3,
				pix->width * COBALT_BYTES_PER_PIXEL_YUYV);
		pix->pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	case V4L2_PIX_FMT_RGB24:
		pix->bytesperline = max(pix->bytesperline & ~0x3,
				pix->width * COBALT_BYTES_PER_PIXEL_RGB24);
		break;
	case V4L2_PIX_FMT_BGR32:
		pix->bytesperline = max(pix->bytesperline & ~0x3,
				pix->width * COBALT_BYTES_PER_PIXEL_RGB32);
		break;
	}

	pix->sizeimage = pix->bytesperline * pix->height;
	pix->field = V4L2_FIELD_NONE;
	pix->priv = 0;

	return 0;
}

static int cobalt_s_fmt_vid_cap(struct file *file, void *priv_fh,
		struct v4l2_format *f)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (vb2_is_busy(&s->q))
		return -EBUSY;

	if (cobalt_try_fmt_vid_cap(file, priv_fh, f))
		return -EINVAL;

	s->width = pix->width;
	s->height = pix->height;
	s->stride = pix->bytesperline;
	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		s->bpp = COBALT_BYTES_PER_PIXEL_YUYV;
		break;
	case V4L2_PIX_FMT_RGB24:
		s->bpp = COBALT_BYTES_PER_PIXEL_RGB24;
		break;
	case V4L2_PIX_FMT_BGR32:
		s->bpp = COBALT_BYTES_PER_PIXEL_RGB32;
		break;
	default:
		return -EINVAL;
	}
	s->pixfmt = pix->pixelformat;
	cobalt_enable_input(s);

	return 0;
}

static int cobalt_try_fmt_vid_out(struct file *file, void *priv_fh,
		struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	/* Check for min (QCIF) and max (Full HD) size */
	if ((pix->width < 176) || (pix->height < 144)) {
		pix->width = 176;
		pix->height = 144;
	}

	if ((pix->width > 1920) || (pix->height > 1080)) {
		pix->width = 1920;
		pix->height = 1080;
	}

	/* Make width multiple of 4 */
	pix->width &= ~0x3;

	/* Make height multiple of 2 */
	pix->height &= ~0x1;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	default:
		pix->bytesperline = max(pix->bytesperline & ~0x3,
				pix->width * COBALT_BYTES_PER_PIXEL_YUYV);
		pix->pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	case V4L2_PIX_FMT_BGR32:
		pix->bytesperline = max(pix->bytesperline & ~0x3,
				pix->width * COBALT_BYTES_PER_PIXEL_RGB32);
		break;
	}

	pix->sizeimage = pix->bytesperline * pix->height;
	pix->field = V4L2_FIELD_NONE;

	return 0;
}

static int cobalt_g_fmt_vid_out(struct file *file, void *priv_fh,
		struct v4l2_format *f)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = s->width;
	pix->height = s->height;
	pix->bytesperline = s->stride;
	pix->field = V4L2_FIELD_NONE;
	pix->pixelformat = s->pixfmt;
	pix->colorspace = s->colorspace;
	pix->xfer_func = s->xfer_func;
	pix->ycbcr_enc = s->ycbcr_enc;
	pix->quantization = s->quantization;
	pix->sizeimage = pix->bytesperline * pix->height;

	return 0;
}

static int cobalt_enum_fmt_vid_out(struct file *file, void *priv_fh,
		struct v4l2_fmtdesc *f)
{
	switch (f->index) {
	case 0:
		strlcpy(f->description, "YUV 4:2:2", sizeof(f->description));
		f->pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	case 1:
		strlcpy(f->description, "RGB32", sizeof(f->description));
		f->pixelformat = V4L2_PIX_FMT_BGR32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cobalt_s_fmt_vid_out(struct file *file, void *priv_fh,
		struct v4l2_format *f)
{
	struct cobalt_stream *s = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev_format sd_fmt = { 0 };
	u32 code;

	if (cobalt_try_fmt_vid_out(file, priv_fh, f))
		return -EINVAL;

	if (vb2_is_busy(&s->q) && (pix->pixelformat != s->pixfmt ||
	    pix->width != s->width || pix->height != s->height ||
	    pix->bytesperline != s->stride))
		return -EBUSY;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		s->bpp = COBALT_BYTES_PER_PIXEL_YUYV;
		code = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	case V4L2_PIX_FMT_BGR32:
		s->bpp = COBALT_BYTES_PER_PIXEL_RGB32;
		code = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	default:
		return -EINVAL;
	}
	s->width = pix->width;
	s->height = pix->height;
	s->stride = pix->bytesperline;
	s->pixfmt = pix->pixelformat;
	s->colorspace = pix->colorspace;
	s->xfer_func = pix->xfer_func;
	s->ycbcr_enc = pix->ycbcr_enc;
	s->quantization = pix->quantization;
	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	v4l2_fill_mbus_format(&sd_fmt.format, pix, code);
	v4l2_subdev_call(s->sd, pad, set_fmt, NULL, &sd_fmt);
	return 0;
}

static int cobalt_enum_input(struct file *file, void *priv_fh,
				 struct v4l2_input *inp)
{
	struct cobalt_stream *s = video_drvdata(file);

	if (inp->index > 1)
		return -EINVAL;
	if (inp->index == 0)
		snprintf(inp->name, sizeof(inp->name),
				"HDMI-%d", s->video_channel);
	else
		snprintf(inp->name, sizeof(inp->name),
				"Generator-%d", s->video_channel);
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	if (inp->index == 1)
		return 0;
	return v4l2_subdev_call(s->sd,
			video, g_input_status, &inp->status);
}

static int cobalt_g_input(struct file *file, void *priv_fh, unsigned int *i)
{
	struct cobalt_stream *s = video_drvdata(file);

	*i = s->input;
	return 0;
}

static int cobalt_s_input(struct file *file, void *priv_fh, unsigned int i)
{
	struct cobalt_stream *s = video_drvdata(file);

	if (i >= 2)
		return -EINVAL;
	if (vb2_is_busy(&s->q))
		return -EBUSY;
	s->input = i;

	cobalt_enable_input(s);

	if (s->input == 1) /* Test Pattern Generator */
		return 0;

	return v4l2_subdev_call(s->sd, video, s_routing,
			ADV76XX_PAD_HDMI_PORT_A, 0, 0);
}

static int cobalt_enum_output(struct file *file, void *priv_fh,
				 struct v4l2_output *out)
{
	if (out->index)
		return -EINVAL;
	snprintf(out->name, sizeof(out->name), "HDMI-%d", out->index);
	out->type = V4L2_OUTPUT_TYPE_ANALOG;
	out->capabilities = V4L2_OUT_CAP_DV_TIMINGS;
	return 0;
}

static int cobalt_g_output(struct file *file, void *priv_fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cobalt_s_output(struct file *file, void *priv_fh, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int cobalt_g_edid(struct file *file, void *fh, struct v4l2_edid *edid)
{
	struct cobalt_stream *s = video_drvdata(file);
	u32 pad = edid->pad;
	int ret;

	if (edid->pad >= (s->is_output ? 1 : 2))
		return -EINVAL;
	edid->pad = 0;
	ret = v4l2_subdev_call(s->sd, pad, get_edid, edid);
	edid->pad = pad;
	return ret;
}

static int cobalt_s_edid(struct file *file, void *fh, struct v4l2_edid *edid)
{
	struct cobalt_stream *s = video_drvdata(file);
	u32 pad = edid->pad;
	int ret;

	if (edid->pad >= 2)
		return -EINVAL;
	edid->pad = 0;
	ret = v4l2_subdev_call(s->sd, pad, set_edid, edid);
	edid->pad = pad;
	return ret;
}

static int cobalt_subscribe_event(struct v4l2_fh *fh,
				  const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_event_subscribe(fh, sub, 4, NULL);
	}
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static int cobalt_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	a->parm.capture.timeperframe.numerator = 1;
	a->parm.capture.timeperframe.denominator = 60;
	a->parm.capture.readbuffers = 3;
	return 0;
}

static const struct v4l2_ioctl_ops cobalt_ioctl_ops = {
	.vidioc_querycap		= cobalt_querycap,
	.vidioc_g_parm			= cobalt_g_parm,
	.vidioc_log_status		= cobalt_log_status,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_enum_input		= cobalt_enum_input,
	.vidioc_g_input			= cobalt_g_input,
	.vidioc_s_input			= cobalt_s_input,
	.vidioc_enum_fmt_vid_cap	= cobalt_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= cobalt_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= cobalt_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= cobalt_try_fmt_vid_cap,
	.vidioc_enum_output		= cobalt_enum_output,
	.vidioc_g_output		= cobalt_g_output,
	.vidioc_s_output		= cobalt_s_output,
	.vidioc_enum_fmt_vid_out	= cobalt_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out		= cobalt_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= cobalt_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= cobalt_try_fmt_vid_out,
	.vidioc_s_dv_timings		= cobalt_s_dv_timings,
	.vidioc_g_dv_timings		= cobalt_g_dv_timings,
	.vidioc_query_dv_timings	= cobalt_query_dv_timings,
	.vidioc_enum_dv_timings		= cobalt_enum_dv_timings,
	.vidioc_dv_timings_cap		= cobalt_dv_timings_cap,
	.vidioc_g_edid			= cobalt_g_edid,
	.vidioc_s_edid			= cobalt_s_edid,
	.vidioc_subscribe_event		= cobalt_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register              = cobalt_g_register,
	.vidioc_s_register              = cobalt_s_register,
#endif
};

static const struct v4l2_ioctl_ops cobalt_ioctl_empty_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register              = cobalt_g_register,
	.vidioc_s_register              = cobalt_s_register,
#endif
};

/* Register device nodes */

static const struct v4l2_file_operations cobalt_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.unlocked_ioctl = video_ioctl2,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.read = vb2_fop_read,
};

static const struct v4l2_file_operations cobalt_out_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.unlocked_ioctl = video_ioctl2,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.write = vb2_fop_write,
};

static const struct v4l2_file_operations cobalt_empty_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.unlocked_ioctl = video_ioctl2,
	.release = v4l2_fh_release,
};

static int cobalt_node_register(struct cobalt *cobalt, int node)
{
	static const struct v4l2_dv_timings dv1080p60 =
		V4L2_DV_BT_CEA_1920X1080P60;
	struct cobalt_stream *s = cobalt->streams + node;
	struct video_device *vdev = &s->vdev;
	struct vb2_queue *q = &s->q;
	int ret;

	mutex_init(&s->lock);
	spin_lock_init(&s->irqlock);

	snprintf(vdev->name, sizeof(vdev->name),
			"%s-%d", cobalt->v4l2_dev.name, node);
	s->width = 1920;
	/* Audio frames are just 4 lines of 1920 bytes */
	s->height = s->is_audio ? 4 : 1080;

	if (s->is_audio) {
		s->bpp = 1;
		s->pixfmt = V4L2_PIX_FMT_GREY;
	} else if (s->is_output) {
		s->bpp = COBALT_BYTES_PER_PIXEL_RGB32;
		s->pixfmt = V4L2_PIX_FMT_BGR32;
	} else {
		s->bpp = COBALT_BYTES_PER_PIXEL_YUYV;
		s->pixfmt = V4L2_PIX_FMT_YUYV;
	}
	s->colorspace = V4L2_COLORSPACE_SRGB;
	s->stride = s->width * s->bpp;

	if (!s->is_audio) {
		if (s->is_dummy)
			cobalt_warn("Setting up dummy video node %d\n", node);
		vdev->v4l2_dev = &cobalt->v4l2_dev;
		if (s->is_dummy)
			vdev->fops = &cobalt_empty_fops;
		else
			vdev->fops = s->is_output ? &cobalt_out_fops :
						    &cobalt_fops;
		vdev->release = video_device_release_empty;
		vdev->vfl_dir = s->is_output ? VFL_DIR_TX : VFL_DIR_RX;
		vdev->lock = &s->lock;
		if (s->sd)
			vdev->ctrl_handler = s->sd->ctrl_handler;
		s->timings = dv1080p60;
		v4l2_subdev_call(s->sd, video, s_dv_timings, &s->timings);
		if (!s->is_output && s->sd)
			cobalt_enable_input(s);
		vdev->ioctl_ops = s->is_dummy ? &cobalt_ioctl_empty_ops :
				  &cobalt_ioctl_ops;
	}

	INIT_LIST_HEAD(&s->bufs);
	q->type = s->is_output ? V4L2_BUF_TYPE_VIDEO_OUTPUT :
				 V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->io_modes |= s->is_output ? VB2_WRITE : VB2_READ;
	q->drv_priv = s;
	q->buf_struct_size = sizeof(struct cobalt_buffer);
	q->ops = &cobalt_qops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->lock = &s->lock;
	vdev->queue = q;

	video_set_drvdata(vdev, s);
	ret = vb2_queue_init(q);
	if (!s->is_audio && ret == 0)
		ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	else if (!s->is_dummy)
		ret = cobalt_alsa_init(s);

	if (ret < 0) {
		if (!s->is_audio)
			cobalt_err("couldn't register v4l2 device node %d\n",
					node);
		return ret;
	}
	cobalt_info("registered node %d\n", node);
	return 0;
}

/* Initialize v4l2 variables and register v4l2 devices */
int cobalt_nodes_register(struct cobalt *cobalt)
{
	int node, ret;

	/* Setup V4L2 Devices */
	for (node = 0; node < COBALT_NUM_STREAMS; node++) {
		ret = cobalt_node_register(cobalt, node);
		if (ret)
			return ret;
	}
	return 0;
}

/* Unregister v4l2 devices */
void cobalt_nodes_unregister(struct cobalt *cobalt)
{
	int node;

	/* Teardown all streams */
	for (node = 0; node < COBALT_NUM_STREAMS; node++) {
		struct cobalt_stream *s = cobalt->streams + node;
		struct video_device *vdev = &s->vdev;

		if (!s->is_audio)
			video_unregister_device(vdev);
		else if (!s->is_dummy)
			cobalt_alsa_exit(s);
	}
}
