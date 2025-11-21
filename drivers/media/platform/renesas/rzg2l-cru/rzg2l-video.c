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

#include <media/mipi-csi2.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include "rzg2l-cru.h"
#include "rzg2l-cru-regs.h"

#define RZG2L_TIMEOUT_MS		100
#define RZG2L_RETRIES			10

#define RZG2L_CRU_DEFAULT_FORMAT	V4L2_PIX_FMT_UYVY
#define RZG2L_CRU_DEFAULT_WIDTH		RZG2L_CRU_MIN_INPUT_WIDTH
#define RZG2L_CRU_DEFAULT_HEIGHT	RZG2L_CRU_MIN_INPUT_HEIGHT
#define RZG2L_CRU_DEFAULT_FIELD		V4L2_FIELD_NONE
#define RZG2L_CRU_DEFAULT_COLORSPACE	V4L2_COLORSPACE_SRGB

#define RZG2L_CRU_STRIDE_MAX		32640
#define RZG2L_CRU_STRIDE_ALIGN		128

struct rzg2l_cru_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

#define to_buf_list(vb2_buffer) \
	(&container_of(vb2_buffer, struct rzg2l_cru_buffer, vb)->list)

/* -----------------------------------------------------------------------------
 * DMA operations
 */
static void __rzg2l_cru_write(struct rzg2l_cru_dev *cru, u32 offset, u32 value)
{
	const u16 *regs = cru->info->regs;

	/*
	 * CRUnCTRL is a first register on all CRU supported SoCs so validate
	 * rest of the registers have valid offset being set in cru->info->regs.
	 */
	if (WARN_ON(offset >= RZG2L_CRU_MAX_REG) ||
	    WARN_ON(offset != CRUnCTRL && regs[offset] == 0))
		return;

	iowrite32(value, cru->base + regs[offset]);
}

static u32 __rzg2l_cru_read(struct rzg2l_cru_dev *cru, u32 offset)
{
	const u16 *regs = cru->info->regs;

	/*
	 * CRUnCTRL is a first register on all CRU supported SoCs so validate
	 * rest of the registers have valid offset being set in cru->info->regs.
	 */
	if (WARN_ON(offset >= RZG2L_CRU_MAX_REG) ||
	    WARN_ON(offset != CRUnCTRL && regs[offset] == 0))
		return 0;

	return ioread32(cru->base + regs[offset]);
}

static __always_inline void
__rzg2l_cru_write_constant(struct rzg2l_cru_dev *cru, u32 offset, u32 value)
{
	const u16 *regs = cru->info->regs;

	BUILD_BUG_ON(offset >= RZG2L_CRU_MAX_REG);

	iowrite32(value, cru->base + regs[offset]);
}

static __always_inline u32
__rzg2l_cru_read_constant(struct rzg2l_cru_dev *cru, u32 offset)
{
	const u16 *regs = cru->info->regs;

	BUILD_BUG_ON(offset >= RZG2L_CRU_MAX_REG);

	return ioread32(cru->base + regs[offset]);
}

#define rzg2l_cru_write(cru, offset, value) \
	(__builtin_constant_p(offset) ? \
	 __rzg2l_cru_write_constant(cru, offset, value) : \
	 __rzg2l_cru_write(cru, offset, value))

#define rzg2l_cru_read(cru, offset) \
	(__builtin_constant_p(offset) ? \
	 __rzg2l_cru_read_constant(cru, offset) : \
	 __rzg2l_cru_read(cru, offset))

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

	cru->buf_addr[slot] = addr;
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
	const struct rzg2l_cru_info *info = cru->info;
	unsigned int slot;
	u32 amnaxiattr;

	/*
	 * Set image data memory banks.
	 * Currently, we will use maximum address.
	 */
	rzg2l_cru_write(cru, AMnMBVALID, AMnMBVALID_MBVALID(cru->num_buf - 1));

	for (slot = 0; slot < cru->num_buf; slot++)
		rzg2l_cru_fill_hw_slot(cru, slot);

	if (info->has_stride) {
		u32 stride = cru->format.bytesperline;
		u32 amnis;

		stride /= RZG2L_CRU_STRIDE_ALIGN;
		amnis = rzg2l_cru_read(cru, AMnIS) & ~AMnIS_IS_MASK;
		rzg2l_cru_write(cru, AMnIS, amnis | AMnIS_IS(stride));
	}

	/* Set AXI burst max length to recommended setting */
	amnaxiattr = rzg2l_cru_read(cru, AMnAXIATTR) & ~AMnAXIATTR_AXILEN_MASK;
	amnaxiattr |= AMnAXIATTR_AXILEN;
	rzg2l_cru_write(cru, AMnAXIATTR, amnaxiattr);
}

static void rzg2l_cru_csi2_setup(struct rzg2l_cru_dev *cru,
				 const struct rzg2l_cru_ip_format *ip_fmt,
				 u8 csi_vc)
{
	const struct rzg2l_cru_info *info = cru->info;
	u32 icnmc = ICnMC_INF(ip_fmt->datatype);

	if (cru->info->regs[ICnSVC]) {
		rzg2l_cru_write(cru, ICnSVCNUM, csi_vc);
		rzg2l_cru_write(cru, ICnSVC, ICnSVC_SVC0(0) | ICnSVC_SVC1(1) |
				ICnSVC_SVC2(2) | ICnSVC_SVC3(3));
	}

	icnmc |= rzg2l_cru_read(cru, info->image_conv) & ~ICnMC_INF_MASK;

	/* Set virtual channel CSI2 */
	icnmc |= ICnMC_VCSEL(csi_vc);

	rzg2l_cru_write(cru, info->image_conv, icnmc);
}

static int rzg2l_cru_initialize_image_conv(struct rzg2l_cru_dev *cru,
					   struct v4l2_mbus_framefmt *ip_sd_fmt,
					   u8 csi_vc)
{
	const struct rzg2l_cru_info *info = cru->info;
	const struct rzg2l_cru_ip_format *cru_video_fmt;
	const struct rzg2l_cru_ip_format *cru_ip_fmt;

	cru_ip_fmt = rzg2l_cru_ip_code_to_fmt(ip_sd_fmt->code);
	rzg2l_cru_csi2_setup(cru, cru_ip_fmt, csi_vc);

	/* Output format */
	cru_video_fmt = rzg2l_cru_ip_format_to_fmt(cru->format.pixelformat);
	if (!cru_video_fmt) {
		dev_err(cru->dev, "Invalid pixelformat (0x%x)\n",
			cru->format.pixelformat);
		return -EINVAL;
	}

	/* If input and output use same colorspace, do bypass mode */
	if (cru_ip_fmt->yuv == cru_video_fmt->yuv)
		rzg2l_cru_write(cru, info->image_conv,
				rzg2l_cru_read(cru, info->image_conv) | ICnMC_CSCTHR);
	else
		rzg2l_cru_write(cru, info->image_conv,
				rzg2l_cru_read(cru, info->image_conv) & ~ICnMC_CSCTHR);

	/* Set output data format */
	rzg2l_cru_write(cru, ICnDMR, cru_video_fmt->icndmr);

	return 0;
}

bool rzg3e_fifo_empty(struct rzg2l_cru_dev *cru)
{
	u32 amnfifopntr = rzg2l_cru_read(cru, AMnFIFOPNTR);

	if ((((amnfifopntr & AMnFIFOPNTR_FIFORPNTR_B1) >> 24) ==
	     ((amnfifopntr & AMnFIFOPNTR_FIFOWPNTR_B1) >> 8)) &&
	    (((amnfifopntr & AMnFIFOPNTR_FIFORPNTR_B0) >> 16) ==
	     (amnfifopntr & AMnFIFOPNTR_FIFOWPNTR_B0)))
		return true;

	return false;
}

bool rzg2l_fifo_empty(struct rzg2l_cru_dev *cru)
{
	u32 amnfifopntr, amnfifopntr_w, amnfifopntr_r_y;

	amnfifopntr = rzg2l_cru_read(cru, AMnFIFOPNTR);

	amnfifopntr_w = amnfifopntr & AMnFIFOPNTR_FIFOWPNTR;
	amnfifopntr_r_y =
		(amnfifopntr & AMnFIFOPNTR_FIFORPNTR_Y) >> 16;

	return amnfifopntr_w == amnfifopntr_r_y;
}

void rzg2l_cru_stop_image_processing(struct rzg2l_cru_dev *cru)
{
	unsigned int retries = 0;
	unsigned long flags;
	u32 icnms;

	spin_lock_irqsave(&cru->qlock, flags);

	/* Disable and clear the interrupt */
	cru->info->disable_interrupts(cru);

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
		if (cru->info->fifo_empty(cru))
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

static int rzg2l_cru_get_virtual_channel(struct rzg2l_cru_dev *cru)
{
	struct v4l2_mbus_frame_desc fd = { };
	struct media_pad *remote_pad;
	int ret;

	remote_pad = media_pad_remote_pad_unique(&cru->ip.pads[RZG2L_CRU_IP_SINK]);
	ret = v4l2_subdev_call(cru->ip.remote, pad, get_frame_desc, remote_pad->index, &fd);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		dev_err(cru->dev, "get_frame_desc failed on IP remote subdev\n");
		return ret;
	}
	/* If remote subdev does not implement .get_frame_desc default to VC0. */
	if (ret == -ENOIOCTLCMD)
		return 0;

	if (fd.type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2) {
		dev_err(cru->dev, "get_frame_desc returned invalid bus type %d\n", fd.type);
		return -EINVAL;
	}

	if (!fd.num_entries) {
		dev_err(cru->dev, "get_frame_desc returned zero entries\n");
		return -EINVAL;
	}

	return fd.entry[0].bus.csi2.vc;
}

void rzg3e_cru_enable_interrupts(struct rzg2l_cru_dev *cru)
{
	rzg2l_cru_write(cru, CRUnIE2, CRUnIE2_FSxE(cru->svc_channel));
	rzg2l_cru_write(cru, CRUnIE2, CRUnIE2_FExE(cru->svc_channel));
}

void rzg3e_cru_disable_interrupts(struct rzg2l_cru_dev *cru)
{
	rzg2l_cru_write(cru, CRUnIE, 0);
	rzg2l_cru_write(cru, CRUnIE2, 0);
	rzg2l_cru_write(cru, CRUnINTS, rzg2l_cru_read(cru, CRUnINTS));
	rzg2l_cru_write(cru, CRUnINTS2, rzg2l_cru_read(cru, CRUnINTS2));
}

void rzg2l_cru_enable_interrupts(struct rzg2l_cru_dev *cru)
{
	rzg2l_cru_write(cru, CRUnIE, CRUnIE_EFE);
}

void rzg2l_cru_disable_interrupts(struct rzg2l_cru_dev *cru)
{
	rzg2l_cru_write(cru, CRUnIE, 0);
	rzg2l_cru_write(cru, CRUnINTS, 0x001f000f);
}

int rzg2l_cru_start_image_processing(struct rzg2l_cru_dev *cru)
{
	struct v4l2_mbus_framefmt *fmt = rzg2l_cru_ip_get_src_fmt(cru);
	unsigned long flags;
	u8 csi_vc;
	int ret;

	ret = rzg2l_cru_get_virtual_channel(cru);
	if (ret < 0)
		return ret;
	csi_vc = ret;
	cru->svc_channel = csi_vc;

	spin_lock_irqsave(&cru->qlock, flags);

	/* Select a video input */
	rzg2l_cru_write(cru, CRUnCTRL, CRUnCTRL_VINSEL(0));

	/* Cancel the software reset for image processing block */
	rzg2l_cru_write(cru, CRUnRST, CRUnRST_VRESETN);

	/* Disable and clear the interrupt before using */
	cru->info->disable_interrupts(cru);

	/* Initialize the AXI master */
	rzg2l_cru_initialize_axi(cru);

	/* Initialize image convert */
	ret = rzg2l_cru_initialize_image_conv(cru, fmt, csi_vc);
	if (ret) {
		spin_unlock_irqrestore(&cru->qlock, flags);
		return ret;
	}

	/* Enable interrupt */
	cru->info->enable_interrupts(cru);

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

irqreturn_t rzg2l_cru_irq(int irq, void *data)
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

static int rzg3e_cru_get_current_slot(struct rzg2l_cru_dev *cru)
{
	u64 amnmadrs;
	int slot;

	/*
	 * When AMnMADRSL is read, AMnMADRSH of the higher-order
	 * address also latches the address.
	 *
	 * AMnMADRSH must be read after AMnMADRSL has been read.
	 */
	amnmadrs = rzg2l_cru_read(cru, AMnMADRSL);
	amnmadrs |= (u64)rzg2l_cru_read(cru, AMnMADRSH) << 32;

	/* Ensure amnmadrs is within this buffer range */
	for (slot = 0; slot < cru->num_buf; slot++) {
		if (amnmadrs >= cru->buf_addr[slot] &&
		    amnmadrs < cru->buf_addr[slot] + cru->format.sizeimage)
			return slot;
	}

	dev_err(cru->dev, "Invalid MB address 0x%llx (out of range)\n", amnmadrs);
	return -EINVAL;
}

irqreturn_t rzg3e_cru_irq(int irq, void *data)
{
	struct rzg2l_cru_dev *cru = data;
	u32 irq_status;
	int slot;

	scoped_guard(spinlock, &cru->qlock) {
		irq_status = rzg2l_cru_read(cru, CRUnINTS2);
		if (!irq_status)
			return IRQ_NONE;

		dev_dbg(cru->dev, "CRUnINTS2 0x%x\n", irq_status);

		rzg2l_cru_write(cru, CRUnINTS2, rzg2l_cru_read(cru, CRUnINTS2));

		/* Nothing to do if capture status is 'RZG2L_CRU_DMA_STOPPED' */
		if (cru->state == RZG2L_CRU_DMA_STOPPED) {
			dev_dbg(cru->dev, "IRQ while state stopped\n");
			return IRQ_HANDLED;
		}

		if (cru->state == RZG2L_CRU_DMA_STOPPING) {
			if (irq_status & CRUnINTS2_FSxS(0) ||
			    irq_status & CRUnINTS2_FSxS(1) ||
			    irq_status & CRUnINTS2_FSxS(2) ||
			    irq_status & CRUnINTS2_FSxS(3))
				dev_dbg(cru->dev, "IRQ while state stopping\n");
			return IRQ_HANDLED;
		}

		slot = rzg3e_cru_get_current_slot(cru);
		if (slot < 0)
			return IRQ_HANDLED;

		dev_dbg(cru->dev, "Current written slot: %d\n", slot);
		cru->buf_addr[slot] = 0;

		/*
		 * To hand buffers back in a known order to userspace start
		 * to capture first from slot 0.
		 */
		if (cru->state == RZG2L_CRU_DMA_STARTING) {
			if (slot != 0) {
				dev_dbg(cru->dev, "Starting sync slot: %d\n", slot);
				return IRQ_HANDLED;
			}
			dev_dbg(cru->dev, "Capture start synced!\n");
			cru->state = RZG2L_CRU_DMA_RUNNING;
		}

		/* Capture frame */
		if (cru->queue_buf[slot]) {
			struct vb2_v4l2_buffer *buf = cru->queue_buf[slot];

			buf->field = cru->format.field;
			buf->sequence = cru->sequence;
			buf->vb2_buf.timestamp = ktime_get_ns();
			vb2_buffer_done(&buf->vb2_buf, VB2_BUF_STATE_DONE);
			cru->queue_buf[slot] = NULL;
		} else {
			/* Scratch buffer was used, dropping frame. */
			dev_dbg(cru->dev, "Dropping frame %u\n", cru->sequence);
		}

		cru->sequence++;

		/* Prepare for next frame */
		rzg2l_cru_fill_hw_slot(cru, slot);
	}

	return IRQ_HANDLED;
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

	/* Allocate scratch buffer */
	cru->scratch = dma_alloc_coherent(cru->dev, cru->format.sizeimage,
					  &cru->scratch_phys, GFP_KERNEL);
	if (!cru->scratch) {
		return_unused_buffers(cru, VB2_BUF_STATE_QUEUED);
		dev_err(cru->dev, "Failed to allocate scratch buffer\n");
		ret = -ENOMEM;
		goto assert_presetn;
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

static void rzg2l_cru_format_align(struct rzg2l_cru_dev *cru,
				   struct v4l2_pix_format *pix)
{
	const struct rzg2l_cru_info *info = cru->info;
	const struct rzg2l_cru_ip_format *fmt;

	fmt = rzg2l_cru_ip_format_to_fmt(pix->pixelformat);
	if (!fmt) {
		pix->pixelformat = RZG2L_CRU_DEFAULT_FORMAT;
		fmt = rzg2l_cru_ip_format_to_fmt(pix->pixelformat);
	}

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
	v4l_bound_align_image(&pix->width, 320, info->max_width, 1,
			      &pix->height, 240, info->max_height, 2, 0);

	v4l2_fill_pixfmt(pix, pix->pixelformat, pix->width, pix->height);

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
	const struct rzg2l_cru_ip_format *fmt;

	fmt = rzg2l_cru_ip_index_to_fmt(f->index);
	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->format;

	return 0;
}

static int rzg2l_cru_enum_framesizes(struct file *file, void *fh,
				     struct v4l2_frmsizeenum *fsize)
{
	struct rzg2l_cru_dev *cru = video_drvdata(file);
	const struct rzg2l_cru_info *info = cru->info;
	const struct rzg2l_cru_ip_format *fmt;

	if (fsize->index)
		return -EINVAL;

	fmt = rzg2l_cru_ip_format_to_fmt(fsize->pixel_format);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = RZG2L_CRU_MIN_INPUT_WIDTH;
	fsize->stepwise.max_width = info->max_width;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.min_height = RZG2L_CRU_MIN_INPUT_HEIGHT;
	fsize->stepwise.max_height = info->max_height;
	fsize->stepwise.step_height = 1;

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
	.vidioc_enum_framesizes		= rzg2l_cru_enum_framesizes,
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

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

static int rzg2l_cru_video_link_validate(struct media_link *link)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	const struct rzg2l_cru_ip_format *video_fmt;
	struct v4l2_subdev *subdev;
	struct rzg2l_cru_dev *cru;
	int ret;

	subdev = media_entity_to_v4l2_subdev(link->source->entity);
	fmt.pad = link->source->index;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	cru = container_of(media_entity_to_video_device(link->sink->entity),
			   struct rzg2l_cru_dev, vdev);
	video_fmt = rzg2l_cru_ip_format_to_fmt(cru->format.pixelformat);

	if (fmt.format.width != cru->format.width ||
	    fmt.format.height != cru->format.height ||
	    fmt.format.field != cru->format.field ||
	    !rzg2l_cru_ip_fmt_supports_mbus_code(video_fmt, fmt.format.code))
		return -EPIPE;

	return 0;
}

static const struct media_entity_operations rzg2l_cru_video_media_ops = {
	.link_validate = rzg2l_cru_video_link_validate,
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
	vdev->entity.ops = &rzg2l_cru_video_media_ops;
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
