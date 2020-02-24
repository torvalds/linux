// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 NextThing Co
 * Copyright (C) 2016-2019 Bootlin
 *
 * Author: Maxime Ripard <maxime.ripard@bootlin.com>
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "sun4i_csi.h"

struct sun4i_csi_buffer {
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
};

static inline struct sun4i_csi_buffer *
vb2_v4l2_to_csi_buffer(const struct vb2_v4l2_buffer *p)
{
	return container_of(p, struct sun4i_csi_buffer, vb);
}

static inline struct sun4i_csi_buffer *
vb2_to_csi_buffer(const struct vb2_buffer *p)
{
	return vb2_v4l2_to_csi_buffer(to_vb2_v4l2_buffer(p));
}

static void sun4i_csi_capture_start(struct sun4i_csi *csi)
{
	writel(CSI_CPT_CTRL_VIDEO_START, csi->regs + CSI_CPT_CTRL_REG);
}

static void sun4i_csi_capture_stop(struct sun4i_csi *csi)
{
	writel(0, csi->regs + CSI_CPT_CTRL_REG);
}

static int sun4i_csi_queue_setup(struct vb2_queue *vq,
				 unsigned int *nbuffers,
				 unsigned int *nplanes,
				 unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct sun4i_csi *csi = vb2_get_drv_priv(vq);
	unsigned int num_planes = csi->fmt.num_planes;
	unsigned int i;

	if (*nplanes) {
		if (*nplanes != num_planes)
			return -EINVAL;

		for (i = 0; i < num_planes; i++)
			if (sizes[i] < csi->fmt.plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}

	*nplanes = num_planes;
	for (i = 0; i < num_planes; i++)
		sizes[i] = csi->fmt.plane_fmt[i].sizeimage;

	return 0;
};

static int sun4i_csi_buffer_prepare(struct vb2_buffer *vb)
{
	struct sun4i_csi *csi = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int i;

	for (i = 0; i < csi->fmt.num_planes; i++) {
		unsigned long size = csi->fmt.plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < size) {
			dev_err(csi->dev, "buffer too small (%lu < %lu)\n",
				vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static int sun4i_csi_setup_scratch_buffer(struct sun4i_csi *csi,
					  unsigned int slot)
{
	dma_addr_t addr = csi->scratch.paddr;
	unsigned int plane;

	dev_dbg(csi->dev,
		"No more available buffer, using the scratch buffer\n");

	for (plane = 0; plane < csi->fmt.num_planes; plane++) {
		writel(addr, csi->regs + CSI_BUF_ADDR_REG(plane, slot));
		addr += csi->fmt.plane_fmt[plane].sizeimage;
	}

	csi->current_buf[slot] = NULL;
	return 0;
}

static int sun4i_csi_buffer_fill_slot(struct sun4i_csi *csi, unsigned int slot)
{
	struct sun4i_csi_buffer *c_buf;
	struct vb2_v4l2_buffer *v_buf;
	unsigned int plane;

	/*
	 * We should never end up in a situation where we overwrite an
	 * already filled slot.
	 */
	if (WARN_ON(csi->current_buf[slot]))
		return -EINVAL;

	if (list_empty(&csi->buf_list))
		return sun4i_csi_setup_scratch_buffer(csi, slot);

	c_buf = list_first_entry(&csi->buf_list, struct sun4i_csi_buffer, list);
	list_del_init(&c_buf->list);

	v_buf = &c_buf->vb;
	csi->current_buf[slot] = v_buf;

	for (plane = 0; plane < csi->fmt.num_planes; plane++) {
		dma_addr_t buf_addr;

		buf_addr = vb2_dma_contig_plane_dma_addr(&v_buf->vb2_buf,
							 plane);
		writel(buf_addr, csi->regs + CSI_BUF_ADDR_REG(plane, slot));
	}

	return 0;
}

static int sun4i_csi_buffer_fill_all(struct sun4i_csi *csi)
{
	unsigned int slot;
	int ret;

	for (slot = 0; slot < CSI_MAX_BUFFER; slot++) {
		ret = sun4i_csi_buffer_fill_slot(csi, slot);
		if (ret)
			return ret;
	}

	return 0;
}

static void sun4i_csi_buffer_mark_done(struct sun4i_csi *csi,
				       unsigned int slot,
				       unsigned int sequence)
{
	struct vb2_v4l2_buffer *v_buf;

	if (!csi->current_buf[slot]) {
		dev_dbg(csi->dev, "Scratch buffer was used, ignoring..\n");
		return;
	}

	v_buf = csi->current_buf[slot];
	v_buf->field = csi->fmt.field;
	v_buf->sequence = sequence;
	v_buf->vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&v_buf->vb2_buf, VB2_BUF_STATE_DONE);

	csi->current_buf[slot] = NULL;
}

static int sun4i_csi_buffer_flip(struct sun4i_csi *csi, unsigned int sequence)
{
	u32 reg = readl(csi->regs + CSI_BUF_CTRL_REG);
	unsigned int next;

	/* Our next buffer is not the current buffer */
	next = !(reg & CSI_BUF_CTRL_DBS);

	/* Report the previous buffer as done */
	sun4i_csi_buffer_mark_done(csi, next, sequence);

	/* Put a new buffer in there */
	return sun4i_csi_buffer_fill_slot(csi, next);
}

static void sun4i_csi_buffer_queue(struct vb2_buffer *vb)
{
	struct sun4i_csi *csi = vb2_get_drv_priv(vb->vb2_queue);
	struct sun4i_csi_buffer *buf = vb2_to_csi_buffer(vb);
	unsigned long flags;

	spin_lock_irqsave(&csi->qlock, flags);
	list_add_tail(&buf->list, &csi->buf_list);
	spin_unlock_irqrestore(&csi->qlock, flags);
}

static void return_all_buffers(struct sun4i_csi *csi,
			       enum vb2_buffer_state state)
{
	struct sun4i_csi_buffer *buf, *node;
	unsigned int slot;

	list_for_each_entry_safe(buf, node, &csi->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}

	for (slot = 0; slot < CSI_MAX_BUFFER; slot++) {
		struct vb2_v4l2_buffer *v_buf = csi->current_buf[slot];

		if (!v_buf)
			continue;

		vb2_buffer_done(&v_buf->vb2_buf, state);
		csi->current_buf[slot] = NULL;
	}
}

static int sun4i_csi_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct sun4i_csi *csi = vb2_get_drv_priv(vq);
	struct v4l2_fwnode_bus_parallel *bus = &csi->bus;
	const struct sun4i_csi_format *csi_fmt;
	unsigned long href_pol, pclk_pol, vref_pol;
	unsigned long flags;
	unsigned int i;
	int ret;

	csi_fmt = sun4i_csi_find_format(&csi->fmt.pixelformat, NULL);
	if (!csi_fmt)
		return -EINVAL;

	dev_dbg(csi->dev, "Starting capture\n");

	csi->sequence = 0;

	/*
	 * We need a scratch buffer in case where we'll not have any
	 * more buffer queued so that we don't error out. One of those
	 * cases is when you end up at the last frame to capture, you
	 * don't havea any buffer queued any more, and yet it doesn't
	 * really matter since you'll never reach the next buffer.
	 *
	 * Since we support the multi-planar API, we need to have a
	 * buffer for each plane. Allocating a single one large enough
	 * to hold all the buffers is simpler, so let's go for that.
	 */
	csi->scratch.size = 0;
	for (i = 0; i < csi->fmt.num_planes; i++)
		csi->scratch.size += csi->fmt.plane_fmt[i].sizeimage;

	csi->scratch.vaddr = dma_alloc_coherent(csi->dev,
						csi->scratch.size,
						&csi->scratch.paddr,
						GFP_KERNEL);
	if (!csi->scratch.vaddr) {
		dev_err(csi->dev, "Failed to allocate scratch buffer\n");
		ret = -ENOMEM;
		goto err_clear_dma_queue;
	}

	ret = media_pipeline_start(&csi->vdev.entity, &csi->vdev.pipe);
	if (ret < 0)
		goto err_free_scratch_buffer;

	spin_lock_irqsave(&csi->qlock, flags);

	/* Setup timings */
	writel(CSI_WIN_CTRL_W_ACTIVE(csi->fmt.width * 2),
	       csi->regs + CSI_WIN_CTRL_W_REG);
	writel(CSI_WIN_CTRL_H_ACTIVE(csi->fmt.height),
	       csi->regs + CSI_WIN_CTRL_H_REG);

	/*
	 * This hardware uses [HV]REF instead of [HV]SYNC. Based on the
	 * provided timing diagrams in the manual, positive polarity
	 * equals active high [HV]REF.
	 *
	 * When the back porch is 0, [HV]REF is more or less equivalent
	 * to [HV]SYNC inverted.
	 */
	href_pol = !!(bus->flags & V4L2_MBUS_HSYNC_ACTIVE_LOW);
	vref_pol = !!(bus->flags & V4L2_MBUS_VSYNC_ACTIVE_LOW);
	pclk_pol = !!(bus->flags & V4L2_MBUS_PCLK_SAMPLE_RISING);
	writel(CSI_CFG_INPUT_FMT(csi_fmt->input) |
	       CSI_CFG_OUTPUT_FMT(csi_fmt->output) |
	       CSI_CFG_VREF_POL(vref_pol) |
	       CSI_CFG_HREF_POL(href_pol) |
	       CSI_CFG_PCLK_POL(pclk_pol),
	       csi->regs + CSI_CFG_REG);

	/* Setup buffer length */
	writel(csi->fmt.plane_fmt[0].bytesperline,
	       csi->regs + CSI_BUF_LEN_REG);

	/* Prepare our buffers in hardware */
	ret = sun4i_csi_buffer_fill_all(csi);
	if (ret) {
		spin_unlock_irqrestore(&csi->qlock, flags);
		goto err_disable_pipeline;
	}

	/* Enable double buffering */
	writel(CSI_BUF_CTRL_DBE, csi->regs + CSI_BUF_CTRL_REG);

	/* Clear the pending interrupts */
	writel(CSI_INT_FRM_DONE, csi->regs + 0x34);

	/* Enable frame done interrupt */
	writel(CSI_INT_FRM_DONE, csi->regs + CSI_INT_EN_REG);

	sun4i_csi_capture_start(csi);

	spin_unlock_irqrestore(&csi->qlock, flags);

	ret = v4l2_subdev_call(csi->src_subdev, video, s_stream, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		goto err_disable_device;

	return 0;

err_disable_device:
	sun4i_csi_capture_stop(csi);

err_disable_pipeline:
	media_pipeline_stop(&csi->vdev.entity);

err_free_scratch_buffer:
	dma_free_coherent(csi->dev, csi->scratch.size, csi->scratch.vaddr,
			  csi->scratch.paddr);

err_clear_dma_queue:
	spin_lock_irqsave(&csi->qlock, flags);
	return_all_buffers(csi, VB2_BUF_STATE_QUEUED);
	spin_unlock_irqrestore(&csi->qlock, flags);

	return ret;
}

static void sun4i_csi_stop_streaming(struct vb2_queue *vq)
{
	struct sun4i_csi *csi = vb2_get_drv_priv(vq);
	unsigned long flags;

	dev_dbg(csi->dev, "Stopping capture\n");

	v4l2_subdev_call(csi->src_subdev, video, s_stream, 0);
	sun4i_csi_capture_stop(csi);

	/* Release all active buffers */
	spin_lock_irqsave(&csi->qlock, flags);
	return_all_buffers(csi, VB2_BUF_STATE_ERROR);
	spin_unlock_irqrestore(&csi->qlock, flags);

	media_pipeline_stop(&csi->vdev.entity);

	dma_free_coherent(csi->dev, csi->scratch.size, csi->scratch.vaddr,
			  csi->scratch.paddr);
}

static const struct vb2_ops sun4i_csi_qops = {
	.queue_setup		= sun4i_csi_queue_setup,
	.buf_prepare		= sun4i_csi_buffer_prepare,
	.buf_queue		= sun4i_csi_buffer_queue,
	.start_streaming	= sun4i_csi_start_streaming,
	.stop_streaming		= sun4i_csi_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static irqreturn_t sun4i_csi_irq(int irq, void *data)
{
	struct sun4i_csi *csi = data;
	u32 reg;

	reg = readl(csi->regs + CSI_INT_STA_REG);

	/* Acknowledge the interrupts */
	writel(reg, csi->regs + CSI_INT_STA_REG);

	if (!(reg & CSI_INT_FRM_DONE))
		return IRQ_HANDLED;

	spin_lock(&csi->qlock);
	if (sun4i_csi_buffer_flip(csi, csi->sequence++)) {
		dev_warn(csi->dev, "%s: Flip failed\n", __func__);
		sun4i_csi_capture_stop(csi);
	}
	spin_unlock(&csi->qlock);

	return IRQ_HANDLED;
}

int sun4i_csi_dma_register(struct sun4i_csi *csi, int irq)
{
	struct vb2_queue *q = &csi->queue;
	int ret;
	int i;

	spin_lock_init(&csi->qlock);
	mutex_init(&csi->lock);

	INIT_LIST_HEAD(&csi->buf_list);
	for (i = 0; i < CSI_MAX_BUFFER; i++)
		csi->current_buf[i] = NULL;

	q->min_buffers_needed = 3;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP;
	q->lock = &csi->lock;
	q->drv_priv = csi;
	q->buf_struct_size = sizeof(struct sun4i_csi_buffer);
	q->ops = &sun4i_csi_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->dev = csi->dev;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(csi->dev, "failed to initialize VB2 queue\n");
		goto err_free_mutex;
	}

	ret = v4l2_device_register(csi->dev, &csi->v4l);
	if (ret) {
		dev_err(csi->dev, "Couldn't register the v4l2 device\n");
		goto err_free_queue;
	}

	ret = devm_request_irq(csi->dev, irq, sun4i_csi_irq, 0,
			       dev_name(csi->dev), csi);
	if (ret) {
		dev_err(csi->dev, "Couldn't register our interrupt\n");
		goto err_unregister_device;
	}

	return 0;

err_unregister_device:
	v4l2_device_unregister(&csi->v4l);

err_free_queue:
	vb2_queue_release(q);

err_free_mutex:
	mutex_destroy(&csi->lock);
	return ret;
}

void sun4i_csi_dma_unregister(struct sun4i_csi *csi)
{
	v4l2_device_unregister(&csi->v4l);
	vb2_queue_release(&csi->queue);
	mutex_destroy(&csi->lock);
}
