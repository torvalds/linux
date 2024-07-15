// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Renesas RZ/G2L CRU
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 *
 * Based on Renesas R-Car VIN
 * Copyright (C) 2016 Renesas Electronics Corp.
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 * Copyright (C) 2008 Magnus Damm
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include "rzg2l-cru.h"

/* HW CRU Registers Definition */

/* CRU Control Register */
#define CRUnCTRL			0x0
#define CRUnCTRL_VINSEL(x)		((x) << 0)

/* CRU Interrupt Enable Register */
#define CRUnIE				0x4
#define CRUnIE_EFE			BIT(17)

/* CRU Interrupt Status Register */
#define CRUnINTS			0x8
#define CRUnINTS_SFS			BIT(16)

/* CRU Reset Register */
#define CRUnRST				0xc
#define CRUnRST_VRESETN			BIT(0)

/* Memory Bank Base Address (Lower) Register for CRU Image Data */
#define AMnMBxADDRL(x)			(0x100 + ((x) * 8))

/* Memory Bank Base Address (Higher) Register for CRU Image Data */
#define AMnMBxADDRH(x)			(0x104 + ((x) * 8))

/* Memory Bank Enable Register for CRU Image Data */
#define AMnMBVALID			0x148
#define AMnMBVALID_MBVALID(x)		GENMASK(x, 0)

/* Memory Bank Status Register for CRU Image Data */
#define AMnMBS				0x14c
#define AMnMBS_MBSTS			0x7

/* AXI Master FIFO Pointer Register for CRU Image Data */
#define AMnFIFOPNTR			0x168
#define AMnFIFOPNTR_FIFOWPNTR		GENMASK(7, 0)
#define AMnFIFOPNTR_FIFORPNTR_Y		GENMASK(23, 16)

/* AXI Master Transfer Stop Register for CRU Image Data */
#define AMnAXISTP			0x174
#define AMnAXISTP_AXI_STOP		BIT(0)

/* AXI Master Transfer Stop Status Register for CRU Image Data */
#define AMnAXISTPACK			0x178
#define AMnAXISTPACK_AXI_STOP_ACK	BIT(0)

/* CRU Image Processing Enable Register */
#define ICnEN				0x200
#define ICnEN_ICEN			BIT(0)

/* CRU Image Processing Main Control Register */
#define ICnMC				0x208
#define ICnMC_CSCTHR			BIT(5)
#define ICnMC_INF_YUV8_422		(0x1e << 16)
#define ICnMC_INF_USER			(0x30 << 16)
#define ICnMC_VCSEL(x)			((x) << 22)
#define ICnMC_INF_MASK			GENMASK(21, 16)

/* CRU Module Status Register */
#define ICnMS				0x254
#define ICnMS_IA			BIT(2)

/* CRU Data Output Mode Register */
#define ICnDMR				0x26c
#define ICnDMR_YCMODE_UYVY		(1 << 4)

#define RZG2L_TIMEOUT_MS		100
#define RZG2L_RETRIES			10

#define RZG2L_CRU_DEFAULT_FORMAT	V4L2_PIX_FMT_UYVY
#define RZG2L_CRU_DEFAULT_WIDTH		RZG2L_CRU_MIN_INPUT_WIDTH
#define RZG2L_CRU_DEFAULT_HEIGHT	RZG2L_CRU_MIN_INPUT_HEIGHT
#define RZG2L_CRU_DEFAULT_FIELD		V4L2_FIELD_NONE
#define RZG2L_CRU_DEFAULT_COLORSPACE	V4L2_COLORSPACE_SRGB

struct rzg2l_cru_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

#define to_buf_list(vb2_buffer) \
	(&container_of(vb2_buffer, struct rzg2l_cru_buffer, vb)->list)

/* -----------------------------------------------------------------------------
 * DMA operations
 */
static void rzg2l_cru_write(struct rzg2l_cru_dev *cru, u32 offset, u32 value)
{
	iowrite32(value, cru->base + offset);
}

static u32 rzg2l_cru_read(struct rzg2l_cru_dev *cru, u32 offset)
{
	return ioread32(cru->base + offset);
}

/* Need to hold qlock before calling */
static void return_unused_buffers(struct rzg2l_cru_dev *cru,
				  enum vb2_buffer_state state)
{
	struct rzg2l_cru_buffer *buf, *node;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&cru->qlock, flags);
	for (i = 0; i < cru->num_buf; i++) {
		if (cru->queue_buf[i]) {
			vb2_buffer_done(&cru->queue_buf[i]->vb2_buf,
					state);
			cru->queue_buf[i] = NULL;
		}
	}

	list_for_each_entry_safe(buf, node, &cru->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&cru->qlock, flags);
}

static int rzg2l_cru_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				 unsigned int *nplanes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct rzg2l_cru_dev *cru = vb2_get_drv_priv(vq);

	/* Make sure the image size is large enough. */
	if (*nplanes)
		return sizes[0] < cru->format.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = cru->format.sizeimage;

	return 0;
};

static int rzg2l_cru_buffer_prepare(struct vb2_buffer *vb)
{
	struct rzg2l_cru_dev *cru = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = cru->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(cru->dev, "buffer too small (%lu < %lu)\n",
			vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void rzg2l_cru_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rzg2l_cru_dev *cru = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long flags;

	spin_lock_irqsave(&cru->qlock, flags);

	list_add_tail(to_buf_list(vbuf), &cru->buf_list);

	spin_unlock_irqrestore(&cru->qlock, flags);
}

static int rzg2l_cru_mc_validate_format(struct rzg2l_cru_dev *cru,
					struct v4l2_subdev *sd,
					struct media_pad *pad)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	fmt.pad = pad->index;
	if (v4l2_subdev_call_state_active(sd, pad, get_fmt, &fmt))
		return -EPIPE;

	switch (fmt.format.code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
		break;
	default:
		return -EPIPE;
	}

	switch (fmt.format.field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
		break;
	default:
		return -EPIPE;
	}

	if (fmt.format.width != cru->format.width ||
	    fmt.format.height != cru->format.height)
		return -EPIPE;

	return 0;
}

static void rzg2l_cru_set_slot_addr(struct rzg2l_cru_dev *cru,
				    int slot, dma_addr_t addr)
{
	/*
	 * The address needs to be 512 bytes aligned. Driver should never accept
	 * settings that do not satisfy this in the first place...
	 */
	if (WARN_ON((addr) & RZG2L_CRU_HW_BUFFER_MASK))
		return;

	/* Currently, we just use the buffer in 32 bits address */
	rzg2l_cru_write(cru, AMnMBxADDRL(slot), addr);
	rzg2l_cru_write(cru, AMnMBxADDRH(slot), 0);
}

/*
 * Moves a buffer from the queue to the HW slot. If no buffer is
 * available use the scratch buffer. The scratch buffer is never
 * returned to userspace, its only function is to enable the capture
 * loop to keep running.
 */
static void rzg2l_cru_fill_hw_slot(struct rzg2l_cru_dev *cru, int slot)
{
	struct vb2_v4l2_buffer *vbuf;
	struct rzg2l_cru_buffer *buf;
	dma_addr_t phys_addr;

	/* A already populated slot shall never be overwritten. */
	if (WARN_ON(cru->queue_buf[slot]))
		return;

	dev_dbg(cru->dev, "Filling HW slot: %d\n", slot);

	if (list_empty(&cru->buf_list)) {
		cru->queue_buf[slot] = NULL;
		phys_addr = cru->scratch_phys;
	} else {
		/* Keep track of buffer we give to HW */
		buf = list_entry(cru->buf_list.next,
				 struct rzg2l_cru_buffer, list);
		vbuf = &buf->vb;
		list_del_init(to_buf_list(vbuf));
		cru->queue_buf[slot] = vbuf;

		/* Setup DMA */
		phys_addr = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);
	}

	rzg2l_cru_set_slot_addr(cru, slot, phys_addr);
}

static void rzg2l_cru_initialize_axi(struct rzg2l_cru_dev *cru)
{
	unsigned int slot;

	/*
	 * Set image data memory banks.
	 * Currently, we will use maximum address.
	 */
	rzg2l_cru_write(cru, AMnMBVALID, AMnMBVALID_MBVALID(cru->num_buf - 1));

	for (slot = 0; slot < cru->num_buf; slot++)
		rzg2l_cru_fill_hw_slot(cru, slot);
}

static void rzg2l_cru_csi2_setup(struct rzg2l_cru_dev *cru, bool *input_is_yuv,
				 struct v4l2_mbus_framefmt *ip_sd_fmt)
{
	u32 icnmc;

	switch (ip_sd_fmt->code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
		icnmc = ICnMC_INF_YUV8_422;
		*input_is_yuv = true;
		break;
	default:
		*input_is_yuv = false;
		icnmc = ICnMC_INF_USER;
		break;
	}

	icnmc |= (rzg2l_cru_read(cru, ICnMC) & ~ICnMC_INF_MASK);

	/* Set virtual channel CSI2 */
	icnmc |= ICnMC_VCSEL(cru->csi.channel);

	rzg2l_cru_write(cru, ICnMC, icnmc);
}

static int rzg2l_cru_initialize_image_conv(struct rzg2l_cru_dev *cru,
					   struct v4l2_mbus_framefmt *ip_sd_fmt)
{
	bool output_is_yuv = false;
	bool input_is_yuv = false;
	u32 icndmr;

	rzg2l_cru_csi2_setup(cru, &input_is_yuv, ip_sd_fmt);

	/* Output format */
	switch (cru->format.pixelformat) {
	case V4L2_PIX_FMT_UYVY:
		icndmr = ICnDMR_YCMODE_UYVY;
		output_is_yuv = true;
		break;
	default:
		dev_err(cru->dev, "Invalid pixelformat (0x%x)\n",
			cru->format.pixelformat);
		return -EINVAL;
	}

	/* If input and output use same colorspace, do bypass mode */
	if (output_is_yuv == input_is_yuv)
		rzg2l_cru_write(cru, ICnMC,
				rzg2l_cru_read(cru, ICnMC) | ICnMC_CSCTHR);
	else
		rzg2l_cru_write(cru, ICnMC,
				rzg2l_cru_read(cru, ICnMC) & (~ICnMC_CSCTHR));

	/* Set output data format */
	rzg2l_cru_write(cru, ICnDMR, icndmr);

	return 0;
}

void rzg2l_cru_stop_image_processing(struct rzg2l_cru_dev *cru)
{
	u32 amnfifopntr, amnfifopntr_w, amnfifopntr_r_y;
	unsigned int retries = 0;
	unsigned long flags;
	u32 icnms;

	spin_lock_irqsave(&cru->qlock, flags);

	/* Disable and clear the interrupt */
	rzg2l_cru_write(cru, CRUnIE, 0);
	rzg2l_cru_write(cru, CRUnINTS, 0x001F0F0F);

	/* Stop the operation of image conversion */
	rzg2l_cru_write(cru, ICnEN, 0);

	/* Wait for streaming to stop */
	while ((rzg2l_cru_read(cru, ICnMS) & ICnMS_IA) && retries++ < RZG2L_RETRIES) {
		spin_unlock_irqrestore(&cru->qlock, flags);
		msleep(RZG2L_TIMEOUT_MS);
		spin_lock_irqsave(&cru->qlock, flags);
	}

	icnms = rzg2l_cru_read(cru, ICnMS) & ICnMS_IA;
	if (icnms)
		dev_err(cru->dev, "Failed stop HW, something is seriously broken\n");

	cru->state = RZG2L_CRU_DMA_STOPPED;

	/* Wait until the FIFO becomes empty */
	for (retries = 5; retries > 0; retries--) {
		amnfifopntr = rzg2l_cru_read(cru, AMnFIFOPNTR);

		amnfifopntr_w = amnfifopntr & AMnFIFOPNTR_FIFOWPNTR;
		amnfifopntr_r_y =
			(amnfifopntr & AMnFIFOPNTR_FIFORPNTR_Y) >> 16;
		if (amnfifopntr_w == amnfifopntr_r_y)
			break;

		usleep_range(10, 20);
	}

	/* Notify that FIFO is not empty here */
	if (!retries)
		dev_err(cru->dev, "Failed to empty FIFO\n");

	/* Stop AXI bus */
	rzg2l_cru_write(cru, AMnAXISTP, AMnAXISTP_AXI_STOP);

	/* Wait until the AXI bus stop */
	for (retries = 5; retries > 0; retries--) {
		if (rzg2l_cru_read(cru, AMnAXISTPACK) &
			AMnAXISTPACK_AXI_STOP_ACK)
			break;

		usleep_range(10, 20);
	}

	/* Notify that AXI bus can not stop here */
	if (!retries)
		dev_err(cru->dev, "Failed to stop AXI bus\n");

	/* Cancel the AXI bus stop request */
	rzg2l_cru_write(cru, AMnAXISTP, 0);

	/* Reset the CRU (AXI-master) */
	reset_control_assert(cru->aresetn);

	/* Resets the image processing module */
	rzg2l_cru_write(cru, CRUnRST, 0);

	spin_unlock_irqrestore(&cru->qlock, flags);
}

int rzg2l_cru_start_image_processing(struct rzg2l_cru_dev *cru)
{
	struct v4l2_mbus_framefmt *fmt = rzg2l_cru_ip_get_src_fmt(cru);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cru->qlock, flags);

	/* Select a video input */
	rzg2l_cru_write(cru, CRUnCTRL, CRUnCTRL_VINSEL(0));

	/* Cancel the software reset for image processing block */
	rzg2l_cru_write(cru, CRUnRST, CRUnRST_VRESETN);

	/* Disable and clear the interrupt before using */
	rzg2l_cru_write(cru, CRUnIE, 0);
	rzg2l_cru_write(cru, CRUnINTS, 0x001f000f);

	/* Initialize the AXI master */
	rzg2l_cru_initialize_axi(cru);

	/* Initialize image convert */
	ret = rzg2l_cru_initialize_image_conv(cru, fmt);
	if (ret) {
		spin_unlock_irqrestore(&cru->qlock, flags);
		return ret;
	}

	/* Enable interrupt */
	rzg2l_cru_write(cru, CRUnIE, CRUnIE_EFE);

	/* Enable image processing reception */
	rzg2l_cru_write(cru, ICnEN, ICnEN_ICEN);

	spin_unlock_irqrestore(&cru->qlock, flags);

	return 0;
}

static int rzg2l_cru_set_stream(struct rzg2l_cru_dev *cru, int on)
{
	struct media_pipeline *pipe;
	struct v4l2_subdev *sd;
	struct media_pad *pad;
	int ret;

	pad = media_pad_remote_pad_first(&cru->pad);
	if (!pad)
		return -EPIPE;

	sd = media_entity_to_v4l2_subdev(pad->entity);

	if (!on) {
		int stream_off_ret = 0;

		ret = v4l2_subdev_call(sd, video, s_stream, 0);
		if (ret)
			stream_off_ret = ret;

		ret = v4l2_subdev_call(sd, video, post_streamoff);
		if (ret == -ENOIOCTLCMD)
			ret = 0;
		if (ret && !stream_off_ret)
			stream_off_ret = ret;

		video_device_pipeline_stop(&cru->vdev);

		return stream_off_ret;
	}

	ret = rzg2l_cru_mc_validate_format(cru, sd, pad);
	if (ret)
		return ret;

	pipe = media_entity_pipeline(&sd->entity) ? : &cru->vdev.pipe;
	ret = video_device_pipeline_start(&cru->vdev, pipe);
	if (ret)
		return ret;

	ret = v4l2_subdev_call(sd, video, pre_streamon, 0);
	if (ret && ret != -ENOIOCTLCMD)
		goto pipe_line_stop;

	ret = v4l2_subdev_call(sd, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto err_s_stream;

	return 0;

err_s_stream:
	v4l2_subdev_call(sd, video, post_streamoff);

pipe_line_stop:
	video_device_pipeline_stop(&cru->vdev);

	return ret;
}

static void rzg2l_cru_stop_streaming(struct rzg2l_cru_dev *cru)
{
	cru->state = RZG2L_CRU_DMA_STOPPING;

	rzg2l_cru_set_stream(cru, 0);
}

static irqreturn_t rzg2l_cru_irq(int irq, void *data)
{
	struct rzg2l_cru_dev *cru = data;
	unsigned int handled = 0;
	unsigned long flags;
	u32 irq_status;
	u32 amnmbs;
	int slot;

	spin_lock_irqsave(&cru->qlock, flags);

	irq_status = rzg2l_cru_read(cru, CRUnINTS);
	if (!irq_status)
		goto done;

	handled = 1;

	rzg2l_cru_write(cru, CRUnINTS, rzg2l_cru_read(cru, CRUnINTS));

	/* Nothing to do if capture status is 'RZG2L_CRU_DMA_STOPPED' */
	if (cru->state == RZG2L_CRU_DMA_STOPPED) {
		dev_dbg(cru->dev, "IRQ while state stopped\n");
		goto done;
	}

	/* Increase stop retries if capture status is 'RZG2L_CRU_DMA_STOPPING' */
	if (cru->state == RZG2L_CRU_DMA_STOPPING) {
		if (irq_status & CRUnINTS_SFS)
			dev_dbg(cru->dev, "IRQ while state stopping\n");
		goto done;
	}

	/* Prepare for capture and update state */
	amnmbs = rzg2l_cru_read(cru, AMnMBS);
	slot = amnmbs & AMnMBS_MBSTS;

	/*
	 * AMnMBS.MBSTS indicates the destination of Memory Bank (MB).
	 * Recalculate to get the current transfer complete MB.
	 */
	if (slot == 0)
		slot = cru->num_buf - 1;
	else
		slot--;

	/*
	 * To hand buffers back in a known order to userspace start
	 * to capture first from slot 0.
	 */
	if (cru->state == RZG2L_CRU_DMA_STARTING) {
		if (slot != 0) {
			dev_dbg(cru->dev, "Starting sync slot: %d\n", slot);
			goto done;
		}

		dev_dbg(cru->dev, "Capture start synced!\n");
		cru->state = RZG2L_CRU_DMA_RUNNING;
	}

	/* Capture frame */
	if (cru->queue_buf[slot]) {
		cru->queue_buf[slot]->field = cru->format.field;
		cru->queue_buf[slot]->sequence = cru->sequence;
		cru->queue_buf[slot]->vb2_buf.timestamp = ktime_get_ns();
		vb2_buffer_done(&cru->queue_buf[slot]->vb2_buf,
				VB2_BUF_STATE_DONE);
		cru->queue_buf[slot] = NULL;
	} else {
		/* Scratch buffer was used, dropping frame. */
		dev_dbg(cru->dev, "Dropping frame %u\n", cru->sequence);
	}

	cru->sequence++;

	/* Prepare for next frame */
	rzg2l_cru_fill_hw_slot(cru, slot);

done:
	spin_unlock_irqrestore(&cru->qlock, flags);

	return IRQ_RETVAL(handled);
}

static int rzg2l_cru_start_streaming_vq(struct vb2_queue *vq, unsigned int count)
{
	struct rzg2l_cru_dev *cru = vb2_get_drv_priv(vq);
	int ret;

	ret = pm_runtime_resume_and_get(cru->dev);
	if (ret)
		return ret;

	ret = clk_prepare_enable(cru->vclk);
	if (ret)
		goto err_pm_put;

	/* Release reset state */
	ret = reset_control_deassert(cru->aresetn);
	if (ret) {
		dev_err(cru->dev, "failed to deassert aresetn\n");
		goto err_vclk_disable;
	}

	ret = reset_control_deassert(cru->presetn);
	if (ret) {
		reset_control_assert(cru->aresetn);
		dev_err(cru->dev, "failed to deassert presetn\n");
		goto assert_aresetn;
	}

	ret = request_irq(cru->image_conv_irq, rzg2l_cru_irq,
			  IRQF_SHARED, KBUILD_MODNAME, cru);
	if (ret) {
		dev_err(cru->dev, "failed to request irq\n");
		goto assert_presetn;
	}

	/* Allocate scratch buffer. */
	cru->scratch = dma_alloc_coherent(cru->dev, cru->format.sizeimage,
					  &cru->scratch_phys, GFP_KERNEL);
	if (!cru->scratch) {
		return_unused_buffers(cru, VB2_BUF_STATE_QUEUED);
		dev_err(cru->dev, "Failed to allocate scratch buffer\n");
		ret = -ENOMEM;
		goto free_image_conv_irq;
	}

	cru->sequence = 0;

	ret = rzg2l_cru_set_stream(cru, 1);
	if (ret) {
		return_unused_buffers(cru, VB2_BUF_STATE_QUEUED);
		goto out;
	}

	cru->state = RZG2L_CRU_DMA_STARTING;
	dev_dbg(cru->dev, "Starting to capture\n");
	return 0;

out:
	if (ret)
		dma_free_coherent(cru->dev, cru->format.sizeimage, cru->scratch,
				  cru->scratch_phys);
free_image_conv_irq:
	free_irq(cru->image_conv_irq, cru);

assert_presetn:
	reset_control_assert(cru->presetn);

assert_aresetn:
	reset_control_assert(cru->aresetn);

err_vclk_disable:
	clk_disable_unprepare(cru->vclk);

err_pm_put:
	pm_runtime_put_sync(cru->dev);

	return ret;
}

static void rzg2l_cru_stop_streaming_vq(struct vb2_queue *vq)
{
	struct rzg2l_cru_dev *cru = vb2_get_drv_priv(vq);

	rzg2l_cru_stop_streaming(cru);

	/* Free scratch buffer */
	dma_free_coherent(cru->dev, cru->format.sizeimage,
			  cru->scratch, cru->scratch_phys);

	free_irq(cru->image_conv_irq, cru);
	return_unused_buffers(cru, VB2_BUF_STATE_ERROR);

	reset_control_assert(cru->presetn);
	clk_disable_unprepare(cru->vclk);
	pm_runtime_put_sync(cru->dev);
}

static const struct vb2_ops rzg2l_cru_qops = {
	.queue_setup		= rzg2l_cru_queue_setup,
	.buf_prepare		= rzg2l_cru_buffer_prepare,
	.buf_queue		= rzg2l_cru_buffer_queue,
	.start_streaming	= rzg2l_cru_start_streaming_vq,
	.stop_streaming		= rzg2l_cru_stop_streaming_vq,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

void rzg2l_cru_dma_unregister(struct rzg2l_cru_dev *cru)
{
	mutex_destroy(&cru->lock);

	v4l2_device_unregister(&cru->v4l2_dev);
	vb2_queue_release(&cru->queue);
}

int rzg2l_cru_dma_register(struct rzg2l_cru_dev *cru)
{
	struct vb2_queue *q = &cru->queue;
	unsigned int i;
	int ret;

	/* Initialize the top-level structure */
	ret = v4l2_device_register(cru->dev, &cru->v4l2_dev);
	if (ret)
		return ret;

	mutex_init(&cru->lock);
	INIT_LIST_HEAD(&cru->buf_list);

	spin_lock_init(&cru->qlock);

	cru->state = RZG2L_CRU_DMA_STOPPED;

	for (i = 0; i < RZG2L_CRU_HW_BUFFER_MAX; i++)
		cru->queue_buf[i] = NULL;

	/* buffer queue */
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->lock = &cru->lock;
	q->drv_priv = cru;
	q->buf_struct_size = sizeof(struct rzg2l_cru_buffer);
	q->ops = &rzg2l_cru_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 4;
	q->dev = cru->dev;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(cru->dev, "failed to initialize VB2 queue\n");
		goto error;
	}

	return 0;

error:
	mutex_destroy(&cru->lock);
	v4l2_device_unregister(&cru->v4l2_dev);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 stuff
 */

static const struct v4l2_format_info rzg2l_cru_formats[] = {
	{
		.format = V4L2_PIX_FMT_UYVY,
		.bpp[0] = 2,
	},
};

const struct v4l2_format_info *rzg2l_cru_format_from_pixel(u32 format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rzg2l_cru_formats); i++)
		if (rzg2l_cru_formats[i].format == format)
			return rzg2l_cru_formats + i;

	return NULL;
}

static u32 rzg2l_cru_format_bytesperline(struct v4l2_pix_format *pix)
{
	const struct v4l2_format_info *fmt;

	fmt = rzg2l_cru_format_from_pixel(pix->pixelformat);

	if (WARN_ON(!fmt))
		return -EINVAL;

	return pix->width * fmt->bpp[0];
}

static u32 rzg2l_cru_format_sizeimage(struct v4l2_pix_format *pix)
{
	return pix->bytesperline * pix->height;
}

static void rzg2l_cru_format_align(struct rzg2l_cru_dev *cru,
				   struct v4l2_pix_format *pix)
{
	if (!rzg2l_cru_format_from_pixel(pix->pixelformat))
		pix->pixelformat = RZG2L_CRU_DEFAULT_FORMAT;

	switch (pix->field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		pix->field = RZG2L_CRU_DEFAULT_FIELD;
		break;
	}

	/* Limit to CRU capabilities */
	v4l_bound_align_image(&pix->width, 320, RZG2L_CRU_MAX_INPUT_WIDTH, 1,
			      &pix->height, 240, RZG2L_CRU_MAX_INPUT_HEIGHT, 2, 0);

	pix->bytesperline = rzg2l_cru_format_bytesperline(pix);
	pix->sizeimage = rzg2l_cru_format_sizeimage(pix);

	dev_dbg(cru->dev, "Format %ux%u bpl: %u size: %u\n",
		pix->width, pix->height, pix->bytesperline, pix->sizeimage);
}

static void rzg2l_cru_try_format(struct rzg2l_cru_dev *cru,
				 struct v4l2_pix_format *pix)
{
	/*
	 * The V4L2 specification clearly documents the colorspace fields
	 * as being set by drivers for capture devices. Using the values
	 * supplied by userspace thus wouldn't comply with the API. Until
	 * the API is updated force fixed values.
	 */
	pix->colorspace = RZG2L_CRU_DEFAULT_COLORSPACE;
	pix->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);
	pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
	pix->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true, pix->colorspace,
							  pix->ycbcr_enc);

	rzg2l_cru_format_align(cru, pix);
}

static int rzg2l_cru_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, "RZG2L_CRU", sizeof(cap->card));

	return 0;
}

static int rzg2l_cru_try_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct rzg2l_cru_dev *cru = video_drvdata(file);

	rzg2l_cru_try_format(cru, &f->fmt.pix);

	return 0;
}

static int rzg2l_cru_s_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct rzg2l_cru_dev *cru = video_drvdata(file);

	if (vb2_is_busy(&cru->queue))
		return -EBUSY;

	rzg2l_cru_try_format(cru, &f->fmt.pix);

	cru->format = f->fmt.pix;

	return 0;
}

static int rzg2l_cru_g_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct rzg2l_cru_dev *cru = video_drvdata(file);

	f->fmt.pix = cru->format;

	return 0;
}

static int rzg2l_cru_enum_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(rzg2l_cru_formats))
		return -EINVAL;

	f->pixelformat = rzg2l_cru_formats[f->index].format;

	return 0;
}

static const struct v4l2_ioctl_ops rzg2l_cru_ioctl_ops = {
	.vidioc_querycap		= rzg2l_cru_querycap,
	.vidioc_try_fmt_vid_cap		= rzg2l_cru_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= rzg2l_cru_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= rzg2l_cru_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= rzg2l_cru_enum_fmt_vid_cap,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * Media controller file operations
 */

static int rzg2l_cru_open(struct file *file)
{
	struct rzg2l_cru_dev *cru = video_drvdata(file);
	int ret;

	ret = mutex_lock_interruptible(&cru->lock);
	if (ret)
		return ret;

	file->private_data = cru;
	ret = v4l2_fh_open(file);
	if (ret)
		goto err_unlock;

	mutex_unlock(&cru->lock);

	return 0;

err_unlock:
	mutex_unlock(&cru->lock);

	return ret;
}

static int rzg2l_cru_release(struct file *file)
{
	struct rzg2l_cru_dev *cru = video_drvdata(file);
	int ret;

	mutex_lock(&cru->lock);

	/* the release helper will cleanup any on-going streaming. */
	ret = _vb2_fop_release(file, NULL);

	mutex_unlock(&cru->lock);

	return ret;
}

static const struct v4l2_file_operations rzg2l_cru_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= rzg2l_cru_open,
	.release	= rzg2l_cru_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.read		= vb2_fop_read,
};

static void rzg2l_cru_v4l2_init(struct rzg2l_cru_dev *cru)
{
	struct video_device *vdev = &cru->vdev;

	vdev->v4l2_dev = &cru->v4l2_dev;
	vdev->queue = &cru->queue;
	snprintf(vdev->name, sizeof(vdev->name), "CRU output");
	vdev->release = video_device_release_empty;
	vdev->lock = &cru->lock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	vdev->device_caps |= V4L2_CAP_IO_MC;
	vdev->fops = &rzg2l_cru_fops;
	vdev->ioctl_ops = &rzg2l_cru_ioctl_ops;

	/* Set a default format */
	cru->format.pixelformat	= RZG2L_CRU_DEFAULT_FORMAT;
	cru->format.width = RZG2L_CRU_DEFAULT_WIDTH;
	cru->format.height = RZG2L_CRU_DEFAULT_HEIGHT;
	cru->format.field = RZG2L_CRU_DEFAULT_FIELD;
	cru->format.colorspace = RZG2L_CRU_DEFAULT_COLORSPACE;
	rzg2l_cru_format_align(cru, &cru->format);
}

void rzg2l_cru_video_unregister(struct rzg2l_cru_dev *cru)
{
	media_device_unregister(&cru->mdev);
	video_unregister_device(&cru->vdev);
}

int rzg2l_cru_video_register(struct rzg2l_cru_dev *cru)
{
	struct video_device *vdev = &cru->vdev;
	int ret;

	if (video_is_registered(&cru->vdev)) {
		struct media_entity *entity;

		entity = &cru->vdev.entity;
		if (!entity->graph_obj.mdev)
			entity->graph_obj.mdev = &cru->mdev;
		return 0;
	}

	rzg2l_cru_v4l2_init(cru);
	video_set_drvdata(vdev, cru);
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(cru->dev, "Failed to register video device\n");
		return ret;
	}

	ret = media_device_register(&cru->mdev);
	if (ret) {
		video_unregister_device(&cru->vdev);
		return ret;
	}

	return 0;
}
