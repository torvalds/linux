// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Digital Camera Memory Interface
 *
 * Copyright (C) STMicroelectronics SA 2017
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 *          for STMicroelectronics.
 *
 * This driver is based on atmel_isi.c
 *
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-rect.h>
#include <media/videobuf2-dma-contig.h>

#define DRV_NAME "stm32-dcmi"

/* Registers offset for DCMI */
#define DCMI_CR		0x00 /* Control Register */
#define DCMI_SR		0x04 /* Status Register */
#define DCMI_RIS	0x08 /* Raw Interrupt Status register */
#define DCMI_IER	0x0C /* Interrupt Enable Register */
#define DCMI_MIS	0x10 /* Masked Interrupt Status register */
#define DCMI_ICR	0x14 /* Interrupt Clear Register */
#define DCMI_ESCR	0x18 /* Embedded Synchronization Code Register */
#define DCMI_ESUR	0x1C /* Embedded Synchronization Unmask Register */
#define DCMI_CWSTRT	0x20 /* Crop Window STaRT */
#define DCMI_CWSIZE	0x24 /* Crop Window SIZE */
#define DCMI_DR		0x28 /* Data Register */
#define DCMI_IDR	0x2C /* IDentifier Register */

/* Bits definition for control register (DCMI_CR) */
#define CR_CAPTURE	BIT(0)
#define CR_CM		BIT(1)
#define CR_CROP		BIT(2)
#define CR_JPEG		BIT(3)
#define CR_ESS		BIT(4)
#define CR_PCKPOL	BIT(5)
#define CR_HSPOL	BIT(6)
#define CR_VSPOL	BIT(7)
#define CR_FCRC_0	BIT(8)
#define CR_FCRC_1	BIT(9)
#define CR_EDM_0	BIT(10)
#define CR_EDM_1	BIT(11)
#define CR_ENABLE	BIT(14)

/* Bits definition for status register (DCMI_SR) */
#define SR_HSYNC	BIT(0)
#define SR_VSYNC	BIT(1)
#define SR_FNE		BIT(2)

/*
 * Bits definition for interrupt registers
 * (DCMI_RIS, DCMI_IER, DCMI_MIS, DCMI_ICR)
 */
#define IT_FRAME	BIT(0)
#define IT_OVR		BIT(1)
#define IT_ERR		BIT(2)
#define IT_VSYNC	BIT(3)
#define IT_LINE		BIT(4)

enum state {
	STOPPED = 0,
	WAIT_FOR_BUFFER,
	RUNNING,
};

#define MIN_WIDTH	16U
#define MAX_WIDTH	2592U
#define MIN_HEIGHT	16U
#define MAX_HEIGHT	2592U

#define TIMEOUT_MS	1000

#define OVERRUN_ERROR_THRESHOLD	3

struct dcmi_graph_entity {
	struct device_node *node;

	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;
};

struct dcmi_format {
	u32	fourcc;
	u32	mbus_code;
	u8	bpp;
};

struct dcmi_framesize {
	u32	width;
	u32	height;
};

struct dcmi_buf {
	struct vb2_v4l2_buffer	vb;
	bool			prepared;
	dma_addr_t		paddr;
	size_t			size;
	struct list_head	list;
};

struct stm32_dcmi {
	/* Protects the access of variables shared within the interrupt */
	spinlock_t			irqlock;
	struct device			*dev;
	void __iomem			*regs;
	struct resource			*res;
	struct reset_control		*rstc;
	int				sequence;
	struct list_head		buffers;
	struct dcmi_buf			*active;

	struct v4l2_device		v4l2_dev;
	struct video_device		*vdev;
	struct v4l2_async_notifier	notifier;
	struct dcmi_graph_entity	entity;
	struct v4l2_format		fmt;
	struct v4l2_rect		crop;
	bool				do_crop;

	const struct dcmi_format	**sd_formats;
	unsigned int			num_of_sd_formats;
	const struct dcmi_format	*sd_format;
	struct dcmi_framesize		*sd_framesizes;
	unsigned int			num_of_sd_framesizes;
	struct dcmi_framesize		sd_framesize;
	struct v4l2_rect		sd_bounds;

	/* Protect this data structure */
	struct mutex			lock;
	struct vb2_queue		queue;

	struct v4l2_fwnode_bus_parallel	bus;
	struct completion		complete;
	struct clk			*mclk;
	enum state			state;
	struct dma_chan			*dma_chan;
	dma_cookie_t			dma_cookie;
	u32				misr;
	int				errors_count;
	int				overrun_count;
	int				buffers_count;

	/* Ensure DMA operations atomicity */
	struct mutex			dma_lock;
};

static inline struct stm32_dcmi *notifier_to_dcmi(struct v4l2_async_notifier *n)
{
	return container_of(n, struct stm32_dcmi, notifier);
}

static inline u32 reg_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

static inline void reg_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static inline void reg_set(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) | mask);
}

static inline void reg_clear(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) & ~mask);
}

static int dcmi_start_capture(struct stm32_dcmi *dcmi, struct dcmi_buf *buf);

static void dcmi_buffer_done(struct stm32_dcmi *dcmi,
			     struct dcmi_buf *buf,
			     size_t bytesused,
			     int err)
{
	struct vb2_v4l2_buffer *vbuf;

	if (!buf)
		return;

	list_del_init(&buf->list);

	vbuf = &buf->vb;

	vbuf->sequence = dcmi->sequence++;
	vbuf->field = V4L2_FIELD_NONE;
	vbuf->vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&vbuf->vb2_buf, 0, bytesused);
	vb2_buffer_done(&vbuf->vb2_buf,
			err ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	dev_dbg(dcmi->dev, "buffer[%d] done seq=%d, bytesused=%zu\n",
		vbuf->vb2_buf.index, vbuf->sequence, bytesused);

	dcmi->buffers_count++;
	dcmi->active = NULL;
}

static int dcmi_restart_capture(struct stm32_dcmi *dcmi)
{
	struct dcmi_buf *buf;

	spin_lock_irq(&dcmi->irqlock);

	if (dcmi->state != RUNNING) {
		spin_unlock_irq(&dcmi->irqlock);
		return -EINVAL;
	}

	/* Restart a new DMA transfer with next buffer */
	if (list_empty(&dcmi->buffers)) {
		dev_dbg(dcmi->dev, "Capture restart is deferred to next buffer queueing\n");
		dcmi->state = WAIT_FOR_BUFFER;
		spin_unlock_irq(&dcmi->irqlock);
		return 0;
	}
	buf = list_entry(dcmi->buffers.next, struct dcmi_buf, list);
	dcmi->active = buf;

	spin_unlock_irq(&dcmi->irqlock);

	return dcmi_start_capture(dcmi, buf);
}

static void dcmi_dma_callback(void *param)
{
	struct stm32_dcmi *dcmi = (struct stm32_dcmi *)param;
	struct dma_tx_state state;
	enum dma_status status;
	struct dcmi_buf *buf = dcmi->active;

	spin_lock_irq(&dcmi->irqlock);

	/* Check DMA status */
	status = dmaengine_tx_status(dcmi->dma_chan, dcmi->dma_cookie, &state);

	switch (status) {
	case DMA_IN_PROGRESS:
		dev_dbg(dcmi->dev, "%s: Received DMA_IN_PROGRESS\n", __func__);
		break;
	case DMA_PAUSED:
		dev_err(dcmi->dev, "%s: Received DMA_PAUSED\n", __func__);
		break;
	case DMA_ERROR:
		dev_err(dcmi->dev, "%s: Received DMA_ERROR\n", __func__);

		/* Return buffer to V4L2 in error state */
		dcmi_buffer_done(dcmi, buf, 0, -EIO);
		break;
	case DMA_COMPLETE:
		dev_dbg(dcmi->dev, "%s: Received DMA_COMPLETE\n", __func__);

		/* Return buffer to V4L2 */
		dcmi_buffer_done(dcmi, buf, buf->size, 0);

		spin_unlock_irq(&dcmi->irqlock);

		/* Restart capture */
		if (dcmi_restart_capture(dcmi))
			dev_err(dcmi->dev, "%s: Cannot restart capture on DMA complete\n",
				__func__);
		return;
	default:
		dev_err(dcmi->dev, "%s: Received unknown status\n", __func__);
		break;
	}

	spin_unlock_irq(&dcmi->irqlock);
}

static int dcmi_start_dma(struct stm32_dcmi *dcmi,
			  struct dcmi_buf *buf)
{
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_slave_config config;
	int ret;

	memset(&config, 0, sizeof(config));

	config.src_addr = (dma_addr_t)dcmi->res->start + DCMI_DR;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_maxburst = 4;

	/* Configure DMA channel */
	ret = dmaengine_slave_config(dcmi->dma_chan, &config);
	if (ret < 0) {
		dev_err(dcmi->dev, "%s: DMA channel config failed (%d)\n",
			__func__, ret);
		return ret;
	}

	/*
	 * Avoid call of dmaengine_terminate_all() between
	 * dmaengine_prep_slave_single() and dmaengine_submit()
	 * by locking the whole DMA submission sequence
	 */
	mutex_lock(&dcmi->dma_lock);

	/* Prepare a DMA transaction */
	desc = dmaengine_prep_slave_single(dcmi->dma_chan, buf->paddr,
					   buf->size,
					   DMA_DEV_TO_MEM,
					   DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dcmi->dev, "%s: DMA dmaengine_prep_slave_single failed for buffer phy=%pad size=%zu\n",
			__func__, &buf->paddr, buf->size);
		mutex_unlock(&dcmi->dma_lock);
		return -EINVAL;
	}

	/* Set completion callback routine for notification */
	desc->callback = dcmi_dma_callback;
	desc->callback_param = dcmi;

	/* Push current DMA transaction in the pending queue */
	dcmi->dma_cookie = dmaengine_submit(desc);
	if (dma_submit_error(dcmi->dma_cookie)) {
		dev_err(dcmi->dev, "%s: DMA submission failed\n", __func__);
		mutex_unlock(&dcmi->dma_lock);
		return -ENXIO;
	}

	mutex_unlock(&dcmi->dma_lock);

	dma_async_issue_pending(dcmi->dma_chan);

	return 0;
}

static int dcmi_start_capture(struct stm32_dcmi *dcmi, struct dcmi_buf *buf)
{
	int ret;

	if (!buf)
		return -EINVAL;

	ret = dcmi_start_dma(dcmi, buf);
	if (ret) {
		dcmi->errors_count++;
		return ret;
	}

	/* Enable capture */
	reg_set(dcmi->regs, DCMI_CR, CR_CAPTURE);

	return 0;
}

static void dcmi_set_crop(struct stm32_dcmi *dcmi)
{
	u32 size, start;

	/* Crop resolution */
	size = ((dcmi->crop.height - 1) << 16) |
		((dcmi->crop.width << 1) - 1);
	reg_write(dcmi->regs, DCMI_CWSIZE, size);

	/* Crop start point */
	start = ((dcmi->crop.top) << 16) |
		 ((dcmi->crop.left << 1));
	reg_write(dcmi->regs, DCMI_CWSTRT, start);

	dev_dbg(dcmi->dev, "Cropping to %ux%u@%u:%u\n",
		dcmi->crop.width, dcmi->crop.height,
		dcmi->crop.left, dcmi->crop.top);

	/* Enable crop */
	reg_set(dcmi->regs, DCMI_CR, CR_CROP);
}

static void dcmi_process_jpeg(struct stm32_dcmi *dcmi)
{
	struct dma_tx_state state;
	enum dma_status status;
	struct dcmi_buf *buf = dcmi->active;

	if (!buf)
		return;

	/*
	 * Because of variable JPEG buffer size sent by sensor,
	 * DMA transfer never completes due to transfer size never reached.
	 * In order to ensure that all the JPEG data are transferred
	 * in active buffer memory, DMA is drained.
	 * Then DMA tx status gives the amount of data transferred
	 * to memory, which is then returned to V4L2 through the active
	 * buffer payload.
	 */

	/* Drain DMA */
	dmaengine_synchronize(dcmi->dma_chan);

	/* Get DMA residue to get JPEG size */
	status = dmaengine_tx_status(dcmi->dma_chan, dcmi->dma_cookie, &state);
	if (status != DMA_ERROR && state.residue < buf->size) {
		/* Return JPEG buffer to V4L2 with received JPEG buffer size */
		dcmi_buffer_done(dcmi, buf, buf->size - state.residue, 0);
	} else {
		dcmi->errors_count++;
		dev_err(dcmi->dev, "%s: Cannot get JPEG size from DMA\n",
			__func__);
		/* Return JPEG buffer to V4L2 in ERROR state */
		dcmi_buffer_done(dcmi, buf, 0, -EIO);
	}

	/* Abort DMA operation */
	dmaengine_terminate_all(dcmi->dma_chan);

	/* Restart capture */
	if (dcmi_restart_capture(dcmi))
		dev_err(dcmi->dev, "%s: Cannot restart capture on JPEG received\n",
			__func__);
}

static irqreturn_t dcmi_irq_thread(int irq, void *arg)
{
	struct stm32_dcmi *dcmi = arg;

	spin_lock_irq(&dcmi->irqlock);

	if (dcmi->misr & IT_OVR) {
		dcmi->overrun_count++;
		if (dcmi->overrun_count > OVERRUN_ERROR_THRESHOLD)
			dcmi->errors_count++;
	}
	if (dcmi->misr & IT_ERR)
		dcmi->errors_count++;

	if (dcmi->sd_format->fourcc == V4L2_PIX_FMT_JPEG &&
	    dcmi->misr & IT_FRAME) {
		/* JPEG received */
		spin_unlock_irq(&dcmi->irqlock);
		dcmi_process_jpeg(dcmi);
		return IRQ_HANDLED;
	}

	spin_unlock_irq(&dcmi->irqlock);
	return IRQ_HANDLED;
}

static irqreturn_t dcmi_irq_callback(int irq, void *arg)
{
	struct stm32_dcmi *dcmi = arg;
	unsigned long flags;

	spin_lock_irqsave(&dcmi->irqlock, flags);

	dcmi->misr = reg_read(dcmi->regs, DCMI_MIS);

	/* Clear interrupt */
	reg_set(dcmi->regs, DCMI_ICR, IT_FRAME | IT_OVR | IT_ERR);

	spin_unlock_irqrestore(&dcmi->irqlock, flags);

	return IRQ_WAKE_THREAD;
}

static int dcmi_queue_setup(struct vb2_queue *vq,
			    unsigned int *nbuffers,
			    unsigned int *nplanes,
			    unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct stm32_dcmi *dcmi = vb2_get_drv_priv(vq);
	unsigned int size;

	size = dcmi->fmt.fmt.pix.sizeimage;

	/* Make sure the image size is large enough */
	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	dev_dbg(dcmi->dev, "Setup queue, count=%d, size=%d\n",
		*nbuffers, size);

	return 0;
}

static int dcmi_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dcmi_buf *buf = container_of(vbuf, struct dcmi_buf, vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static int dcmi_buf_prepare(struct vb2_buffer *vb)
{
	struct stm32_dcmi *dcmi =  vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dcmi_buf *buf = container_of(vbuf, struct dcmi_buf, vb);
	unsigned long size;

	size = dcmi->fmt.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(dcmi->dev, "%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	if (!buf->prepared) {
		/* Get memory addresses */
		buf->paddr =
			vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
		buf->size = vb2_plane_size(&buf->vb.vb2_buf, 0);
		buf->prepared = true;

		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, buf->size);

		dev_dbg(dcmi->dev, "buffer[%d] phy=%pad size=%zu\n",
			vb->index, &buf->paddr, buf->size);
	}

	return 0;
}

static void dcmi_buf_queue(struct vb2_buffer *vb)
{
	struct stm32_dcmi *dcmi =  vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dcmi_buf *buf = container_of(vbuf, struct dcmi_buf, vb);

	spin_lock_irq(&dcmi->irqlock);

	/* Enqueue to video buffers list */
	list_add_tail(&buf->list, &dcmi->buffers);

	if (dcmi->state == WAIT_FOR_BUFFER) {
		dcmi->state = RUNNING;
		dcmi->active = buf;

		dev_dbg(dcmi->dev, "Starting capture on buffer[%d] queued\n",
			buf->vb.vb2_buf.index);

		spin_unlock_irq(&dcmi->irqlock);
		if (dcmi_start_capture(dcmi, buf))
			dev_err(dcmi->dev, "%s: Cannot restart capture on overflow or error\n",
				__func__);
		return;
	}

	spin_unlock_irq(&dcmi->irqlock);
}

static int dcmi_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct stm32_dcmi *dcmi = vb2_get_drv_priv(vq);
	struct dcmi_buf *buf, *node;
	u32 val = 0;
	int ret;

	ret = pm_runtime_get_sync(dcmi->dev);
	if (ret < 0) {
		dev_err(dcmi->dev, "%s: Failed to start streaming, cannot get sync (%d)\n",
			__func__, ret);
		goto err_release_buffers;
	}

	/* Enable stream on the sub device */
	ret = v4l2_subdev_call(dcmi->entity.subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD) {
		dev_err(dcmi->dev, "%s: Failed to start streaming, subdev streamon error",
			__func__);
		goto err_pm_put;
	}

	spin_lock_irq(&dcmi->irqlock);

	/* Set bus width */
	switch (dcmi->bus.bus_width) {
	case 14:
		val |= CR_EDM_0 | CR_EDM_1;
		break;
	case 12:
		val |= CR_EDM_1;
		break;
	case 10:
		val |= CR_EDM_0;
		break;
	default:
		/* Set bus width to 8 bits by default */
		break;
	}

	/* Set vertical synchronization polarity */
	if (dcmi->bus.flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		val |= CR_VSPOL;

	/* Set horizontal synchronization polarity */
	if (dcmi->bus.flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		val |= CR_HSPOL;

	/* Set pixel clock polarity */
	if (dcmi->bus.flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		val |= CR_PCKPOL;

	reg_write(dcmi->regs, DCMI_CR, val);

	/* Set crop */
	if (dcmi->do_crop)
		dcmi_set_crop(dcmi);

	/* Enable jpeg capture */
	if (dcmi->sd_format->fourcc == V4L2_PIX_FMT_JPEG)
		reg_set(dcmi->regs, DCMI_CR, CR_CM);/* Snapshot mode */

	/* Enable dcmi */
	reg_set(dcmi->regs, DCMI_CR, CR_ENABLE);

	dcmi->sequence = 0;
	dcmi->errors_count = 0;
	dcmi->overrun_count = 0;
	dcmi->buffers_count = 0;

	/*
	 * Start transfer if at least one buffer has been queued,
	 * otherwise transfer is deferred at buffer queueing
	 */
	if (list_empty(&dcmi->buffers)) {
		dev_dbg(dcmi->dev, "Start streaming is deferred to next buffer queueing\n");
		dcmi->state = WAIT_FOR_BUFFER;
		spin_unlock_irq(&dcmi->irqlock);
		return 0;
	}

	buf = list_entry(dcmi->buffers.next, struct dcmi_buf, list);
	dcmi->active = buf;

	dcmi->state = RUNNING;

	dev_dbg(dcmi->dev, "Start streaming, starting capture\n");

	spin_unlock_irq(&dcmi->irqlock);
	ret = dcmi_start_capture(dcmi, buf);
	if (ret) {
		dev_err(dcmi->dev, "%s: Start streaming failed, cannot start capture\n",
			__func__);
		goto err_subdev_streamoff;
	}

	/* Enable interruptions */
	if (dcmi->sd_format->fourcc == V4L2_PIX_FMT_JPEG)
		reg_set(dcmi->regs, DCMI_IER, IT_FRAME | IT_OVR | IT_ERR);
	else
		reg_set(dcmi->regs, DCMI_IER, IT_OVR | IT_ERR);

	return 0;

err_subdev_streamoff:
	v4l2_subdev_call(dcmi->entity.subdev, video, s_stream, 0);

err_pm_put:
	pm_runtime_put(dcmi->dev);

err_release_buffers:
	spin_lock_irq(&dcmi->irqlock);
	/*
	 * Return all buffers to vb2 in QUEUED state.
	 * This will give ownership back to userspace
	 */
	list_for_each_entry_safe(buf, node, &dcmi->buffers, list) {
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	dcmi->active = NULL;
	spin_unlock_irq(&dcmi->irqlock);

	return ret;
}

static void dcmi_stop_streaming(struct vb2_queue *vq)
{
	struct stm32_dcmi *dcmi = vb2_get_drv_priv(vq);
	struct dcmi_buf *buf, *node;
	int ret;

	/* Disable stream on the sub device */
	ret = v4l2_subdev_call(dcmi->entity.subdev, video, s_stream, 0);
	if (ret && ret != -ENOIOCTLCMD)
		dev_err(dcmi->dev, "%s: Failed to stop streaming, subdev streamoff error (%d)\n",
			__func__, ret);

	spin_lock_irq(&dcmi->irqlock);

	/* Disable interruptions */
	reg_clear(dcmi->regs, DCMI_IER, IT_FRAME | IT_OVR | IT_ERR);

	/* Disable DCMI */
	reg_clear(dcmi->regs, DCMI_CR, CR_ENABLE);

	/* Return all queued buffers to vb2 in ERROR state */
	list_for_each_entry_safe(buf, node, &dcmi->buffers, list) {
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	dcmi->active = NULL;
	dcmi->state = STOPPED;

	spin_unlock_irq(&dcmi->irqlock);

	/* Stop all pending DMA operations */
	mutex_lock(&dcmi->dma_lock);
	dmaengine_terminate_all(dcmi->dma_chan);
	mutex_unlock(&dcmi->dma_lock);

	pm_runtime_put(dcmi->dev);

	if (dcmi->errors_count)
		dev_warn(dcmi->dev, "Some errors found while streaming: errors=%d (overrun=%d), buffers=%d\n",
			 dcmi->errors_count, dcmi->overrun_count,
			 dcmi->buffers_count);
	dev_dbg(dcmi->dev, "Stop streaming, errors=%d (overrun=%d), buffers=%d\n",
		dcmi->errors_count, dcmi->overrun_count,
		dcmi->buffers_count);
}

static const struct vb2_ops dcmi_video_qops = {
	.queue_setup		= dcmi_queue_setup,
	.buf_init		= dcmi_buf_init,
	.buf_prepare		= dcmi_buf_prepare,
	.buf_queue		= dcmi_buf_queue,
	.start_streaming	= dcmi_start_streaming,
	.stop_streaming		= dcmi_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int dcmi_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *fmt)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);

	*fmt = dcmi->fmt;

	return 0;
}

static const struct dcmi_format *find_format_by_fourcc(struct stm32_dcmi *dcmi,
						       unsigned int fourcc)
{
	unsigned int num_formats = dcmi->num_of_sd_formats;
	const struct dcmi_format *fmt;
	unsigned int i;

	for (i = 0; i < num_formats; i++) {
		fmt = dcmi->sd_formats[i];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

static void __find_outer_frame_size(struct stm32_dcmi *dcmi,
				    struct v4l2_pix_format *pix,
				    struct dcmi_framesize *framesize)
{
	struct dcmi_framesize *match = NULL;
	unsigned int i;
	unsigned int min_err = UINT_MAX;

	for (i = 0; i < dcmi->num_of_sd_framesizes; i++) {
		struct dcmi_framesize *fsize = &dcmi->sd_framesizes[i];
		int w_err = (fsize->width - pix->width);
		int h_err = (fsize->height - pix->height);
		int err = w_err + h_err;

		if (w_err >= 0 && h_err >= 0 && err < min_err) {
			min_err = err;
			match = fsize;
		}
	}
	if (!match)
		match = &dcmi->sd_framesizes[0];

	*framesize = *match;
}

static int dcmi_try_fmt(struct stm32_dcmi *dcmi, struct v4l2_format *f,
			const struct dcmi_format **sd_format,
			struct dcmi_framesize *sd_framesize)
{
	const struct dcmi_format *sd_fmt;
	struct dcmi_framesize sd_fsize;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev_pad_config pad_cfg;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	bool do_crop;
	int ret;

	sd_fmt = find_format_by_fourcc(dcmi, pix->pixelformat);
	if (!sd_fmt) {
		if (!dcmi->num_of_sd_formats)
			return -ENODATA;

		sd_fmt = dcmi->sd_formats[dcmi->num_of_sd_formats - 1];
		pix->pixelformat = sd_fmt->fourcc;
	}

	/* Limit to hardware capabilities */
	pix->width = clamp(pix->width, MIN_WIDTH, MAX_WIDTH);
	pix->height = clamp(pix->height, MIN_HEIGHT, MAX_HEIGHT);

	/* No crop if JPEG is requested */
	do_crop = dcmi->do_crop && (pix->pixelformat != V4L2_PIX_FMT_JPEG);

	if (do_crop && dcmi->num_of_sd_framesizes) {
		struct dcmi_framesize outer_sd_fsize;
		/*
		 * If crop is requested and sensor have discrete frame sizes,
		 * select the frame size that is just larger than request
		 */
		__find_outer_frame_size(dcmi, pix, &outer_sd_fsize);
		pix->width = outer_sd_fsize.width;
		pix->height = outer_sd_fsize.height;
	}

	v4l2_fill_mbus_format(&format.format, pix, sd_fmt->mbus_code);
	ret = v4l2_subdev_call(dcmi->entity.subdev, pad, set_fmt,
			       &pad_cfg, &format);
	if (ret < 0)
		return ret;

	/* Update pix regarding to what sensor can do */
	v4l2_fill_pix_format(pix, &format.format);

	/* Save resolution that sensor can actually do */
	sd_fsize.width = pix->width;
	sd_fsize.height = pix->height;

	if (do_crop) {
		struct v4l2_rect c = dcmi->crop;
		struct v4l2_rect max_rect;

		/*
		 * Adjust crop by making the intersection between
		 * format resolution request and crop request
		 */
		max_rect.top = 0;
		max_rect.left = 0;
		max_rect.width = pix->width;
		max_rect.height = pix->height;
		v4l2_rect_map_inside(&c, &max_rect);
		c.top  = clamp_t(s32, c.top, 0, pix->height - c.height);
		c.left = clamp_t(s32, c.left, 0, pix->width - c.width);
		dcmi->crop = c;

		/* Adjust format resolution request to crop */
		pix->width = dcmi->crop.width;
		pix->height = dcmi->crop.height;
	}

	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * sd_fmt->bpp;
	pix->sizeimage = pix->bytesperline * pix->height;

	if (sd_format)
		*sd_format = sd_fmt;
	if (sd_framesize)
		*sd_framesize = sd_fsize;

	return 0;
}

static int dcmi_set_fmt(struct stm32_dcmi *dcmi, struct v4l2_format *f)
{
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	const struct dcmi_format *sd_format;
	struct dcmi_framesize sd_framesize;
	struct v4l2_mbus_framefmt *mf = &format.format;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	/*
	 * Try format, fmt.width/height could have been changed
	 * to match sensor capability or crop request
	 * sd_format & sd_framesize will contain what subdev
	 * can do for this request.
	 */
	ret = dcmi_try_fmt(dcmi, f, &sd_format, &sd_framesize);
	if (ret)
		return ret;

	/* Disable crop if JPEG is requested */
	if (pix->pixelformat == V4L2_PIX_FMT_JPEG)
		dcmi->do_crop = false;

	/* pix to mbus format */
	v4l2_fill_mbus_format(mf, pix,
			      sd_format->mbus_code);
	mf->width = sd_framesize.width;
	mf->height = sd_framesize.height;

	ret = v4l2_subdev_call(dcmi->entity.subdev, pad,
			       set_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	dev_dbg(dcmi->dev, "Sensor format set to 0x%x %ux%u\n",
		mf->code, mf->width, mf->height);
	dev_dbg(dcmi->dev, "Buffer format set to %4.4s %ux%u\n",
		(char *)&pix->pixelformat,
		pix->width, pix->height);

	dcmi->fmt = *f;
	dcmi->sd_format = sd_format;
	dcmi->sd_framesize = sd_framesize;

	return 0;
}

static int dcmi_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);

	if (vb2_is_streaming(&dcmi->queue))
		return -EBUSY;

	return dcmi_set_fmt(dcmi, f);
}

static int dcmi_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);

	return dcmi_try_fmt(dcmi, f, NULL, NULL);
}

static int dcmi_enum_fmt_vid_cap(struct file *file, void  *priv,
				 struct v4l2_fmtdesc *f)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);

	if (f->index >= dcmi->num_of_sd_formats)
		return -EINVAL;

	f->pixelformat = dcmi->sd_formats[f->index]->fourcc;
	return 0;
}

static int dcmi_get_sensor_format(struct stm32_dcmi *dcmi,
				  struct v4l2_pix_format *pix)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	ret = v4l2_subdev_call(dcmi->entity.subdev, pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	v4l2_fill_pix_format(pix, &fmt.format);

	return 0;
}

static int dcmi_set_sensor_format(struct stm32_dcmi *dcmi,
				  struct v4l2_pix_format *pix)
{
	const struct dcmi_format *sd_fmt;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	struct v4l2_subdev_pad_config pad_cfg;
	int ret;

	sd_fmt = find_format_by_fourcc(dcmi, pix->pixelformat);
	if (!sd_fmt) {
		if (!dcmi->num_of_sd_formats)
			return -ENODATA;

		sd_fmt = dcmi->sd_formats[dcmi->num_of_sd_formats - 1];
		pix->pixelformat = sd_fmt->fourcc;
	}

	v4l2_fill_mbus_format(&format.format, pix, sd_fmt->mbus_code);
	ret = v4l2_subdev_call(dcmi->entity.subdev, pad, set_fmt,
			       &pad_cfg, &format);
	if (ret < 0)
		return ret;

	return 0;
}

static int dcmi_get_sensor_bounds(struct stm32_dcmi *dcmi,
				  struct v4l2_rect *r)
{
	struct v4l2_subdev_selection bounds = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.target = V4L2_SEL_TGT_CROP_BOUNDS,
	};
	unsigned int max_width, max_height, max_pixsize;
	struct v4l2_pix_format pix;
	unsigned int i;
	int ret;

	/*
	 * Get sensor bounds first
	 */
	ret = v4l2_subdev_call(dcmi->entity.subdev, pad, get_selection,
			       NULL, &bounds);
	if (!ret)
		*r = bounds.r;
	if (ret != -ENOIOCTLCMD)
		return ret;

	/*
	 * If selection is not implemented,
	 * fallback by enumerating sensor frame sizes
	 * and take the largest one
	 */
	max_width = 0;
	max_height = 0;
	max_pixsize = 0;
	for (i = 0; i < dcmi->num_of_sd_framesizes; i++) {
		struct dcmi_framesize *fsize = &dcmi->sd_framesizes[i];
		unsigned int pixsize = fsize->width * fsize->height;

		if (pixsize > max_pixsize) {
			max_pixsize = pixsize;
			max_width = fsize->width;
			max_height = fsize->height;
		}
	}
	if (max_pixsize > 0) {
		r->top = 0;
		r->left = 0;
		r->width = max_width;
		r->height = max_height;
		return 0;
	}

	/*
	 * If frame sizes enumeration is not implemented,
	 * fallback by getting current sensor frame size
	 */
	ret = dcmi_get_sensor_format(dcmi, &pix);
	if (ret)
		return ret;

	r->top = 0;
	r->left = 0;
	r->width = pix.width;
	r->height = pix.height;

	return 0;
}

static int dcmi_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r = dcmi->sd_bounds;
		return 0;
	case V4L2_SEL_TGT_CROP:
		if (dcmi->do_crop) {
			s->r = dcmi->crop;
		} else {
			s->r.top = 0;
			s->r.left = 0;
			s->r.width = dcmi->fmt.fmt.pix.width;
			s->r.height = dcmi->fmt.fmt.pix.height;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dcmi_s_selection(struct file *file, void *priv,
			    struct v4l2_selection *s)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);
	struct v4l2_rect r = s->r;
	struct v4l2_rect max_rect;
	struct v4l2_pix_format pix;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    s->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	/* Reset sensor resolution to max resolution */
	pix.pixelformat = dcmi->fmt.fmt.pix.pixelformat;
	pix.width = dcmi->sd_bounds.width;
	pix.height = dcmi->sd_bounds.height;
	dcmi_set_sensor_format(dcmi, &pix);

	/*
	 * Make the intersection between
	 * sensor resolution
	 * and crop request
	 */
	max_rect.top = 0;
	max_rect.left = 0;
	max_rect.width = pix.width;
	max_rect.height = pix.height;
	v4l2_rect_map_inside(&r, &max_rect);
	r.top  = clamp_t(s32, r.top, 0, pix.height - r.height);
	r.left = clamp_t(s32, r.left, 0, pix.width - r.width);

	if (!(r.top == dcmi->sd_bounds.top &&
	      r.left == dcmi->sd_bounds.left &&
	      r.width == dcmi->sd_bounds.width &&
	      r.height == dcmi->sd_bounds.height)) {
		/* Crop if request is different than sensor resolution */
		dcmi->do_crop = true;
		dcmi->crop = r;
		dev_dbg(dcmi->dev, "s_selection: crop %ux%u@(%u,%u) from %ux%u\n",
			r.width, r.height, r.left, r.top,
			pix.width, pix.height);
	} else {
		/* Disable crop */
		dcmi->do_crop = false;
		dev_dbg(dcmi->dev, "s_selection: crop is disabled\n");
	}

	s->r = r;
	return 0;
}

static int dcmi_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	strscpy(cap->driver, DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, "STM32 Camera Memory Interface",
		sizeof(cap->card));
	strscpy(cap->bus_info, "platform:dcmi", sizeof(cap->bus_info));
	return 0;
}

static int dcmi_enum_input(struct file *file, void *priv,
			   struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(i->name, "Camera", sizeof(i->name));
	return 0;
}

static int dcmi_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int dcmi_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;
	return 0;
}

static int dcmi_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);
	const struct dcmi_format *sd_fmt;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	sd_fmt = find_format_by_fourcc(dcmi, fsize->pixel_format);
	if (!sd_fmt)
		return -EINVAL;

	fse.code = sd_fmt->mbus_code;

	ret = v4l2_subdev_call(dcmi->entity.subdev, pad, enum_frame_size,
			       NULL, &fse);
	if (ret)
		return ret;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = fse.max_width;
	fsize->discrete.height = fse.max_height;

	return 0;
}

static int dcmi_g_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *p)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);

	return v4l2_g_parm_cap(video_devdata(file), dcmi->entity.subdev, p);
}

static int dcmi_s_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *p)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);

	return v4l2_s_parm_cap(video_devdata(file), dcmi->entity.subdev, p);
}

static int dcmi_enum_frameintervals(struct file *file, void *fh,
				    struct v4l2_frmivalenum *fival)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);
	const struct dcmi_format *sd_fmt;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = fival->index,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	sd_fmt = find_format_by_fourcc(dcmi, fival->pixel_format);
	if (!sd_fmt)
		return -EINVAL;

	fie.code = sd_fmt->mbus_code;

	ret = v4l2_subdev_call(dcmi->entity.subdev, pad,
			       enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

static const struct of_device_id stm32_dcmi_of_match[] = {
	{ .compatible = "st,stm32-dcmi"},
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, stm32_dcmi_of_match);

static int dcmi_open(struct file *file)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);
	struct v4l2_subdev *sd = dcmi->entity.subdev;
	int ret;

	if (mutex_lock_interruptible(&dcmi->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto unlock;

	if (!v4l2_fh_is_singular_file(file))
		goto fh_rel;

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		goto fh_rel;

	ret = dcmi_set_fmt(dcmi, &dcmi->fmt);
	if (ret)
		v4l2_subdev_call(sd, core, s_power, 0);
fh_rel:
	if (ret)
		v4l2_fh_release(file);
unlock:
	mutex_unlock(&dcmi->lock);
	return ret;
}

static int dcmi_release(struct file *file)
{
	struct stm32_dcmi *dcmi = video_drvdata(file);
	struct v4l2_subdev *sd = dcmi->entity.subdev;
	bool fh_singular;
	int ret;

	mutex_lock(&dcmi->lock);

	fh_singular = v4l2_fh_is_singular_file(file);

	ret = _vb2_fop_release(file, NULL);

	if (fh_singular)
		v4l2_subdev_call(sd, core, s_power, 0);

	mutex_unlock(&dcmi->lock);

	return ret;
}

static const struct v4l2_ioctl_ops dcmi_ioctl_ops = {
	.vidioc_querycap		= dcmi_querycap,

	.vidioc_try_fmt_vid_cap		= dcmi_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= dcmi_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= dcmi_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= dcmi_enum_fmt_vid_cap,
	.vidioc_g_selection		= dcmi_g_selection,
	.vidioc_s_selection		= dcmi_s_selection,

	.vidioc_enum_input		= dcmi_enum_input,
	.vidioc_g_input			= dcmi_g_input,
	.vidioc_s_input			= dcmi_s_input,

	.vidioc_g_parm			= dcmi_g_parm,
	.vidioc_s_parm			= dcmi_s_parm,

	.vidioc_enum_framesizes		= dcmi_enum_framesizes,
	.vidioc_enum_frameintervals	= dcmi_enum_frameintervals,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations dcmi_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= dcmi_open,
	.release	= dcmi_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
#ifndef CONFIG_MMU
	.get_unmapped_area = vb2_fop_get_unmapped_area,
#endif
	.read		= vb2_fop_read,
};

static int dcmi_set_default_fmt(struct stm32_dcmi *dcmi)
{
	struct v4l2_format f = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width		= CIF_WIDTH,
			.height		= CIF_HEIGHT,
			.field		= V4L2_FIELD_NONE,
			.pixelformat	= dcmi->sd_formats[0]->fourcc,
		},
	};
	int ret;

	ret = dcmi_try_fmt(dcmi, &f, NULL, NULL);
	if (ret)
		return ret;
	dcmi->sd_format = dcmi->sd_formats[0];
	dcmi->fmt = f;
	return 0;
}

static const struct dcmi_format dcmi_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mbus_code = MEDIA_BUS_FMT_RGB565_2X8_LE,
		.bpp = 2,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp = 2,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp = 2,
	}, {
		.fourcc = V4L2_PIX_FMT_JPEG,
		.mbus_code = MEDIA_BUS_FMT_JPEG_1X8,
		.bpp = 1,
	},
};

static int dcmi_formats_init(struct stm32_dcmi *dcmi)
{
	const struct dcmi_format *sd_fmts[ARRAY_SIZE(dcmi_formats)];
	unsigned int num_fmts = 0, i, j;
	struct v4l2_subdev *subdev = dcmi->entity.subdev;
	struct v4l2_subdev_mbus_code_enum mbus_code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	while (!v4l2_subdev_call(subdev, pad, enum_mbus_code,
				 NULL, &mbus_code)) {
		for (i = 0; i < ARRAY_SIZE(dcmi_formats); i++) {
			if (dcmi_formats[i].mbus_code != mbus_code.code)
				continue;

			/* Code supported, have we got this fourcc yet? */
			for (j = 0; j < num_fmts; j++)
				if (sd_fmts[j]->fourcc ==
						dcmi_formats[i].fourcc)
					/* Already available */
					break;
			if (j == num_fmts)
				/* New */
				sd_fmts[num_fmts++] = dcmi_formats + i;
		}
		mbus_code.index++;
	}

	if (!num_fmts)
		return -ENXIO;

	dcmi->num_of_sd_formats = num_fmts;
	dcmi->sd_formats = devm_kcalloc(dcmi->dev,
					num_fmts, sizeof(struct dcmi_format *),
					GFP_KERNEL);
	if (!dcmi->sd_formats) {
		dev_err(dcmi->dev, "Could not allocate memory\n");
		return -ENOMEM;
	}

	memcpy(dcmi->sd_formats, sd_fmts,
	       num_fmts * sizeof(struct dcmi_format *));
	dcmi->sd_format = dcmi->sd_formats[0];

	return 0;
}

static int dcmi_framesizes_init(struct stm32_dcmi *dcmi)
{
	unsigned int num_fsize = 0;
	struct v4l2_subdev *subdev = dcmi->entity.subdev;
	struct v4l2_subdev_frame_size_enum fse = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.code = dcmi->sd_format->mbus_code,
	};
	unsigned int ret;
	unsigned int i;

	/* Allocate discrete framesizes array */
	while (!v4l2_subdev_call(subdev, pad, enum_frame_size,
				 NULL, &fse))
		fse.index++;

	num_fsize = fse.index;
	if (!num_fsize)
		return 0;

	dcmi->num_of_sd_framesizes = num_fsize;
	dcmi->sd_framesizes = devm_kcalloc(dcmi->dev, num_fsize,
					   sizeof(struct dcmi_framesize),
					   GFP_KERNEL);
	if (!dcmi->sd_framesizes) {
		dev_err(dcmi->dev, "Could not allocate memory\n");
		return -ENOMEM;
	}

	/* Fill array with sensor supported framesizes */
	dev_dbg(dcmi->dev, "Sensor supports %u frame sizes:\n", num_fsize);
	for (i = 0; i < dcmi->num_of_sd_framesizes; i++) {
		fse.index = i;
		ret = v4l2_subdev_call(subdev, pad, enum_frame_size,
				       NULL, &fse);
		if (ret)
			return ret;
		dcmi->sd_framesizes[fse.index].width = fse.max_width;
		dcmi->sd_framesizes[fse.index].height = fse.max_height;
		dev_dbg(dcmi->dev, "%ux%u\n", fse.max_width, fse.max_height);
	}

	return 0;
}

static int dcmi_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct stm32_dcmi *dcmi = notifier_to_dcmi(notifier);
	int ret;

	dcmi->vdev->ctrl_handler = dcmi->entity.subdev->ctrl_handler;
	ret = dcmi_formats_init(dcmi);
	if (ret) {
		dev_err(dcmi->dev, "No supported mediabus format found\n");
		return ret;
	}

	ret = dcmi_framesizes_init(dcmi);
	if (ret) {
		dev_err(dcmi->dev, "Could not initialize framesizes\n");
		return ret;
	}

	ret = dcmi_get_sensor_bounds(dcmi, &dcmi->sd_bounds);
	if (ret) {
		dev_err(dcmi->dev, "Could not get sensor bounds\n");
		return ret;
	}

	ret = dcmi_set_default_fmt(dcmi);
	if (ret) {
		dev_err(dcmi->dev, "Could not set default format\n");
		return ret;
	}

	ret = video_register_device(dcmi->vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		dev_err(dcmi->dev, "Failed to register video device\n");
		return ret;
	}

	dev_dbg(dcmi->dev, "Device registered as %s\n",
		video_device_node_name(dcmi->vdev));
	return 0;
}

static void dcmi_graph_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *sd,
				     struct v4l2_async_subdev *asd)
{
	struct stm32_dcmi *dcmi = notifier_to_dcmi(notifier);

	dev_dbg(dcmi->dev, "Removing %s\n", video_device_node_name(dcmi->vdev));

	/* Checks internally if vdev has been init or not */
	video_unregister_device(dcmi->vdev);
}

static int dcmi_graph_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct stm32_dcmi *dcmi = notifier_to_dcmi(notifier);

	dev_dbg(dcmi->dev, "Subdev %s bound\n", subdev->name);

	dcmi->entity.subdev = subdev;

	return 0;
}

static const struct v4l2_async_notifier_operations dcmi_graph_notify_ops = {
	.bound = dcmi_graph_notify_bound,
	.unbind = dcmi_graph_notify_unbind,
	.complete = dcmi_graph_notify_complete,
};

static int dcmi_graph_parse(struct stm32_dcmi *dcmi, struct device_node *node)
{
	struct device_node *ep = NULL;
	struct device_node *remote;

	ep = of_graph_get_next_endpoint(node, ep);
	if (!ep)
		return -EINVAL;

	remote = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);
	if (!remote)
		return -EINVAL;

	/* Remote node to connect */
	dcmi->entity.node = remote;
	dcmi->entity.asd.match_type = V4L2_ASYNC_MATCH_FWNODE;
	dcmi->entity.asd.match.fwnode = of_fwnode_handle(remote);
	return 0;
}

static int dcmi_graph_init(struct stm32_dcmi *dcmi)
{
	int ret;

	/* Parse the graph to extract a list of subdevice DT nodes. */
	ret = dcmi_graph_parse(dcmi, dcmi->dev->of_node);
	if (ret < 0) {
		dev_err(dcmi->dev, "Failed to parse graph\n");
		return ret;
	}

	v4l2_async_notifier_init(&dcmi->notifier);

	ret = v4l2_async_notifier_add_subdev(&dcmi->notifier,
					     &dcmi->entity.asd);
	if (ret) {
		dev_err(dcmi->dev, "Failed to add subdev notifier\n");
		of_node_put(dcmi->entity.node);
		return ret;
	}

	dcmi->notifier.ops = &dcmi_graph_notify_ops;

	ret = v4l2_async_notifier_register(&dcmi->v4l2_dev, &dcmi->notifier);
	if (ret < 0) {
		dev_err(dcmi->dev, "Failed to register notifier\n");
		v4l2_async_notifier_cleanup(&dcmi->notifier);
		return ret;
	}

	return 0;
}

static int dcmi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match = NULL;
	struct v4l2_fwnode_endpoint ep = { .bus_type = 0 };
	struct stm32_dcmi *dcmi;
	struct vb2_queue *q;
	struct dma_chan *chan;
	struct clk *mclk;
	int irq;
	int ret = 0;

	match = of_match_device(of_match_ptr(stm32_dcmi_of_match), &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Could not find a match in devicetree\n");
		return -ENODEV;
	}

	dcmi = devm_kzalloc(&pdev->dev, sizeof(struct stm32_dcmi), GFP_KERNEL);
	if (!dcmi)
		return -ENOMEM;

	dcmi->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(dcmi->rstc)) {
		dev_err(&pdev->dev, "Could not get reset control\n");
		return PTR_ERR(dcmi->rstc);
	}

	/* Get bus characteristics from devicetree */
	np = of_graph_get_next_endpoint(np, NULL);
	if (!np) {
		dev_err(&pdev->dev, "Could not find the endpoint\n");
		of_node_put(np);
		return -ENODEV;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(np), &ep);
	of_node_put(np);
	if (ret) {
		dev_err(&pdev->dev, "Could not parse the endpoint\n");
		return ret;
	}

	if (ep.bus_type == V4L2_MBUS_CSI2_DPHY) {
		dev_err(&pdev->dev, "CSI bus not supported\n");
		return -ENODEV;
	}
	dcmi->bus.flags = ep.bus.parallel.flags;
	dcmi->bus.bus_width = ep.bus.parallel.bus_width;
	dcmi->bus.data_shift = ep.bus.parallel.data_shift;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		if (irq != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Could not get irq\n");
		return irq;
	}

	dcmi->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!dcmi->res) {
		dev_err(&pdev->dev, "Could not get resource\n");
		return -ENODEV;
	}

	dcmi->regs = devm_ioremap_resource(&pdev->dev, dcmi->res);
	if (IS_ERR(dcmi->regs)) {
		dev_err(&pdev->dev, "Could not map registers\n");
		return PTR_ERR(dcmi->regs);
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, dcmi_irq_callback,
					dcmi_irq_thread, IRQF_ONESHOT,
					dev_name(&pdev->dev), dcmi);
	if (ret) {
		dev_err(&pdev->dev, "Unable to request irq %d\n", irq);
		return ret;
	}

	mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(mclk)) {
		if (PTR_ERR(mclk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Unable to get mclk\n");
		return PTR_ERR(mclk);
	}

	chan = dma_request_slave_channel(&pdev->dev, "tx");
	if (!chan) {
		dev_info(&pdev->dev, "Unable to request DMA channel, defer probing\n");
		return -EPROBE_DEFER;
	}

	spin_lock_init(&dcmi->irqlock);
	mutex_init(&dcmi->lock);
	mutex_init(&dcmi->dma_lock);
	init_completion(&dcmi->complete);
	INIT_LIST_HEAD(&dcmi->buffers);

	dcmi->dev = &pdev->dev;
	dcmi->mclk = mclk;
	dcmi->state = STOPPED;
	dcmi->dma_chan = chan;

	q = &dcmi->queue;

	/* Initialize the top-level structure */
	ret = v4l2_device_register(&pdev->dev, &dcmi->v4l2_dev);
	if (ret)
		goto err_dma_release;

	dcmi->vdev = video_device_alloc();
	if (!dcmi->vdev) {
		ret = -ENOMEM;
		goto err_device_unregister;
	}

	/* Video node */
	dcmi->vdev->fops = &dcmi_fops;
	dcmi->vdev->v4l2_dev = &dcmi->v4l2_dev;
	dcmi->vdev->queue = &dcmi->queue;
	strscpy(dcmi->vdev->name, KBUILD_MODNAME, sizeof(dcmi->vdev->name));
	dcmi->vdev->release = video_device_release;
	dcmi->vdev->ioctl_ops = &dcmi_ioctl_ops;
	dcmi->vdev->lock = &dcmi->lock;
	dcmi->vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
				  V4L2_CAP_READWRITE;
	video_set_drvdata(dcmi->vdev, dcmi);

	/* Buffer queue */
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF;
	q->lock = &dcmi->lock;
	q->drv_priv = dcmi;
	q->buf_struct_size = sizeof(struct dcmi_buf);
	q->ops = &dcmi_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->dev = &pdev->dev;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize vb2 queue\n");
		goto err_device_release;
	}

	ret = dcmi_graph_init(dcmi);
	if (ret < 0)
		goto err_device_release;

	/* Reset device */
	ret = reset_control_assert(dcmi->rstc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to assert the reset line\n");
		goto err_cleanup;
	}

	usleep_range(3000, 5000);

	ret = reset_control_deassert(dcmi->rstc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to deassert the reset line\n");
		goto err_cleanup;
	}

	dev_info(&pdev->dev, "Probe done\n");

	platform_set_drvdata(pdev, dcmi);

	pm_runtime_enable(&pdev->dev);

	return 0;

err_cleanup:
	v4l2_async_notifier_cleanup(&dcmi->notifier);
err_device_release:
	video_device_release(dcmi->vdev);
err_device_unregister:
	v4l2_device_unregister(&dcmi->v4l2_dev);
err_dma_release:
	dma_release_channel(dcmi->dma_chan);

	return ret;
}

static int dcmi_remove(struct platform_device *pdev)
{
	struct stm32_dcmi *dcmi = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	v4l2_async_notifier_unregister(&dcmi->notifier);
	v4l2_async_notifier_cleanup(&dcmi->notifier);
	v4l2_device_unregister(&dcmi->v4l2_dev);

	dma_release_channel(dcmi->dma_chan);

	return 0;
}

static __maybe_unused int dcmi_runtime_suspend(struct device *dev)
{
	struct stm32_dcmi *dcmi = dev_get_drvdata(dev);

	clk_disable_unprepare(dcmi->mclk);

	return 0;
}

static __maybe_unused int dcmi_runtime_resume(struct device *dev)
{
	struct stm32_dcmi *dcmi = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(dcmi->mclk);
	if (ret)
		dev_err(dev, "%s: Failed to prepare_enable clock\n", __func__);

	return ret;
}

static __maybe_unused int dcmi_suspend(struct device *dev)
{
	/* disable clock */
	pm_runtime_force_suspend(dev);

	/* change pinctrl state */
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static __maybe_unused int dcmi_resume(struct device *dev)
{
	/* restore pinctl default state */
	pinctrl_pm_select_default_state(dev);

	/* clock enable */
	pm_runtime_force_resume(dev);

	return 0;
}

static const struct dev_pm_ops dcmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dcmi_suspend, dcmi_resume)
	SET_RUNTIME_PM_OPS(dcmi_runtime_suspend,
			   dcmi_runtime_resume, NULL)
};

static struct platform_driver stm32_dcmi_driver = {
	.probe		= dcmi_probe,
	.remove		= dcmi_remove,
	.driver		= {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(stm32_dcmi_of_match),
		.pm = &dcmi_pm_ops,
	},
};

module_platform_driver(stm32_dcmi_driver);

MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_AUTHOR("Hugues Fruchet <hugues.fruchet@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 Digital Camera Memory Interface driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("video");
