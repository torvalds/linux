// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/vsp1.h>

#include "risp-core.h"

#define ISP_CS_STREAMER_MODE_REG				0x7000
#define ISP_CS_STREAMER_MODE_STREAMER_EN			0xf

#define ISP_CS_STREAMER_VBLANK_REG				0x7004
#define ISP_CS_STREAMER_HBLANK_REG				0x7008

#define ISP_CS_STREAMER_CONFIG_DMA_CONTROL_REG			0x7100
#define ISP_CS_STREAMER_CONFIG_DMA_REG_ADDRESS_UPPER_8BIT_MASK	GENMASK(31, 24)
#define ISP_CS_STREAMER_CONFIG_DMA_ENABLE0			BIT(0)

#define ISP_CS_STREAMER_CONFIG_DMA_CONTROL1_REG			0x2100
#define ISP_CS_STREAMER_CONFIG_DMA_CONTROL1_ENABLE1		BIT(31)
#define ISP_CS_STREAMER_CONFIG_DMA_CONTROL1_CONFIG_DATA_START_REG_ADDRESS_MASK	GENMASK(15, 0)

#define ISP_CS_STREAMER_CONFIG_DMA_CONTROL2_REG			0x2104

#define ISP_CORE_ISPCORE_INT_STATUS			    0x80000
#define ISP_CORE_ISPCORE_INT_ENABLE			    0x80004
#define ISPCORE_DMA_IMAGE_FRAME_MODE(i, f)		    (0x84000 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_PIXEL_POSITION(i, f)	    (0x84004 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_PIXEL_BITWIDTH_MINUS1(i, f) (0x84008 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_PIXEL_BPP(i, f)		    (0x8400c + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_BASE_ADDRESS_COMP0(i, f)    (0x84010 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_BASE_ADDRESS_COMP1(i, f)    (0x84014 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_BASE_ADDRESS_COMP2(i, f)    (0x84018 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_BASE_ADDRESS_COMP3(i, f)    (0x8401c + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_STRIDE_COMP0(i, f)	    (0x84020 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_STRIDE_COMP1(i, f)	    (0x84024 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_STRIDE_COMP2(i, f)	    (0x84028 + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_STRIDE_COMP3(i, f)	    (0x8402c + 0x1000 * (i) + 0x100 * (f))
#define ISPCORE_DMA_IMAGE_FRAME_AXI_ID(i, f)		    (0x84030 + 0x1000 * (i) + 0x100 * (f))

#define ISPCORE_DMA_IMAGE_FLUSH_OUT_REG(i)			(0x84400 + 0x1000 * (i))
#define ISPCORE_DMA_IMAGE_FLUSH_OUT_PADDING_PIXEL_EOF_MASK	GENMASK(31, 16)
#define ISPCORE_DMA_IMAGE_FLUSH_OUT_PADDING_PIXEL_EOF_SHIFT	16

#define ISPCORE_DMA_IMAGE_AXI_CONFIG_REG(i)			(0x84800 + 0x1000 * (i))

static void risp_cs_write(struct rcar_isp_core *core, u32 offset, u32 value)
{
	iowrite32(value, core->csbase + offset);
}

static u32 risp_cs_read(struct rcar_isp_core *core, u32 offset)
{
	return ioread32(core->csbase + offset);
}

static void risp_core_write(struct rcar_isp_core *core, u32 offset, u32 value)
{
	iowrite32(value, core->base + offset);
}

static u32 risp_core_read(struct rcar_isp_core *core, u32 offset)
{
	return ioread32(core->base + offset);
}

static void risp_core_job_run_params(struct rcar_isp_core *core,
				     struct vsp1_isp_job_desc *vspx_job,
				     struct risp_buffer *buf)
{
	u32 *params_buf = (u32 *)buf->vsp_buffer.cpu_addr;
	bool have_config = !!params_buf[0];
	u32 ctrl0, ctrl1, ctrl2;

	/*
	 * If we have a configuration but not asked the VSPX to program it,
	 * use MMIO to write the configuration. This might be needed to work
	 * around limitations of the VSPX ConfigDMA, see comment in
	 * risp_core_job_prepare().
	 */
	if (have_config && !vspx_job->config.pairs) {
		for (unsigned int i = 0; i < params_buf[0]; i++)
			risp_core_write(core, params_buf[2 + i * 2] & 0xffff,
					params_buf[3 + i * 2]);

		/* Disable ConfigDMA. */
		have_config = false;
	}

	ctrl0 = risp_cs_read(core, ISP_CS_STREAMER_CONFIG_DMA_CONTROL_REG) &
		~ISP_CS_STREAMER_CONFIG_DMA_ENABLE0;
	ctrl1 = risp_cs_read(core, ISP_CS_STREAMER_CONFIG_DMA_CONTROL1_REG) &
		~(ISP_CS_STREAMER_CONFIG_DMA_CONTROL1_ENABLE1 | 0xffff);
	ctrl2 = 0;

	if (have_config) {
		ctrl0 |= ISP_CS_STREAMER_CONFIG_DMA_ENABLE0;
		ctrl1 |= ISP_CS_STREAMER_CONFIG_DMA_CONTROL1_ENABLE1 |
			(params_buf[2] & 0xffff);
		ctrl2 = params_buf[3];
	}

	risp_cs_write(core, ISP_CS_STREAMER_CONFIG_DMA_CONTROL_REG, ctrl0);
	risp_cs_write(core, ISP_CS_STREAMER_CONFIG_DMA_CONTROL1_REG, ctrl1);
	risp_cs_write(core, ISP_CS_STREAMER_CONFIG_DMA_CONTROL2_REG, ctrl2);
}

static void risp_core_job_run_output(struct rcar_isp_core *core,
				     struct risp_buffer *buf)
{
	const struct v4l2_format *fmt = &core->io[RISP_CORE_OUTPUT1].format;
	dma_addr_t mem;
	u32 reg;

	for (unsigned int frame = 0; frame < 4; frame++) {
		reg = ISPCORE_DMA_IMAGE_FRAME_BASE_ADDRESS_COMP0(0, frame);
		mem = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
		risp_core_write(core, reg, mem);

		/* Only NV16 uses 2 planes. */
		if (fmt->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV16M)
			continue;

		reg = ISPCORE_DMA_IMAGE_FRAME_BASE_ADDRESS_COMP1(0, frame);
		mem = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 1);
		risp_core_write(core, reg, mem);
	}
}

static void risp_core_job_run(struct rcar_isp_core *core)
{
	struct rcar_isp_job *job;

	lockdep_assert_held(&core->lock);

	/* ISP not yet started, nothing to do. */
	if (!core->streaming)
		return;

	/* If we have active buffers in the ISP core, nothing to do. */
	if (core->vspx.job)
		return;

	job = list_first_entry_or_null(&core->risp_jobs,
				       struct rcar_isp_job,
				       job_queue);
	if (!job)
		return;

	list_del(&job->job_queue);

	core->vspx.job = job;

	/* Program the ISP register before kicking the VSPX. */
	for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
		struct risp_buffer *buf = job->buffers[i];

		switch (i) {
		case RISP_CORE_PARAMS:
			risp_core_job_run_params(core, &job->vspx_job, buf);
			break;
		case RISP_CORE_OUTPUT1:
			risp_core_job_run_output(core, buf);
			break;
		}
	}

	if (vsp1_isp_job_run(core->vspx.dev, &job->vspx_job)) {
		/*
		 * Release all buffers in this job if running on the VSPX
		 * failed. Userspace should recover from this, no new jobs are
		 * scheduled.
		 */
		for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
			struct risp_buffer *buf = job->buffers[i];

			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		}

		vsp1_isp_job_release(core->vspx.dev, &job->vspx_job);
		core->vspx.job = NULL;
		kfree(job);

		dev_err(core->dev, "Failed to run job");
	}
}

static int risp_core_pixfmt_to_vspx(u32 pixfmt)
{
	switch (pixfmt) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		return V4L2_PIX_FMT_GREY;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		return V4L2_PIX_FMT_Y10;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		return V4L2_PIX_FMT_Y12;
	default:
		return -EINVAL;
	}
}

int risp_core_job_prepare(struct rcar_isp_core *core)
{
	struct vsp1_isp_job_desc *vspx_job;
	int vspx_pixfmt = -EINVAL;
	struct rcar_isp_job *job;
	int ret;

	lockdep_assert_held(&core->io_lock);

	for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
		if (list_empty(&core->io[i].buffers))
			return 0;
	}

	/* Memory is released when the job is consumed. */
	job = kzalloc_obj(*job);
	if (!job)
		return -ENOMEM;

	vspx_job = &job->vspx_job;

	for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
		struct risp_buffer *buf;

		/*
		 * Extract buffer from the IO queue and save a reference in
		 * the job description. Buffers will be completed when the
		 * corresponding frame will be completed by the ISP.
		 */
		buf = list_first_entry_or_null(&core->io[i].buffers,
					       struct risp_buffer, list);
		/*
		 * This should not happen as we have checked there is buffers,
		 * with the lock held, but check the return value anyhow.
		 */
		if (WARN_ON(!buf)) {
			ret = -EINVAL;
			goto error_return_buffers;
		}

		switch (i) {
		case RISP_CORE_INPUT1: {
			u32 isp_pixfmt = core->io[i].format.fmt.pix_mp.pixelformat;

			vspx_pixfmt = risp_core_pixfmt_to_vspx(isp_pixfmt);

			vspx_job->img.fmt = core->io[i].format.fmt.pix_mp;
			vspx_job->img.fmt.pixelformat = vspx_pixfmt;
			vspx_job->img.mem =
				vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf,
							      0);
			break;
		}
		case RISP_CORE_PARAMS: {
			u32 *params_buf = (u32 *)buf->vsp_buffer.cpu_addr;
			u32 pairs = params_buf[0];

			/*
			 * Check config pairs not larger then buffer.
			 *
			 * Remove 8 byte header and each pair is 16 bytes.
			 */
			if (pairs > (RISP_IO_PARAMS_BUF_SIZE - 8) / 16) {
				ret = -EINVAL;
				goto error_return_buffers;
			}

			/*
			 * Work around undocumented behavior of the ConfigDMA
			 * interface by using MMIO if 16 or less pairs are to
			 * be programmed.
			 *
			 * Programming 15 or less pairs corrupts the image data
			 * following the config buffer, programming exactly 16
			 * pairs freeze the whole VSPX.
			 */
			if (pairs <= 16) {
				vspx_job->config.pairs = 0;
			} else {
				vspx_job->config.pairs = pairs;
				vspx_job->config.mem = buf->vsp_buffer.dma_addr;
			}
			break;
		}
		}

		list_del(&buf->list);
		job->buffers[i] = buf;
	}

	if (vspx_pixfmt < 0) {
		ret = -EINVAL;
		goto error_return_buffers;
	}

	ret = vsp1_isp_job_prepare(core->vspx.dev, vspx_job);
	if (ret)
		goto error_return_buffers;

	scoped_guard(spinlock_irqsave, &core->lock) {
		list_add_tail(&job->job_queue, &core->risp_jobs);
		risp_core_job_run(core);
	}

	return 0;

error_return_buffers:
	for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
		if (!job->buffers[i])
			continue;

		vb2_buffer_done(&job->buffers[i]->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
	}
	kfree(job);
	return ret;
}

static int risp_core_config_output(struct rcar_isp_core *core,
				   unsigned int index,
				   const struct v4l2_pix_format_mplane *pix)
{
	/* For all frame capture slots. */
	for (unsigned int frame = 0; frame < 4; frame++) {
		switch (pix->pixelformat) {
		case V4L2_PIX_FMT_NV16M:
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_MODE(index, frame),
					1);
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_PIXEL_POSITION(index, frame),
					0 << 24 | 0 << 16 | 4 << 8 | 16 << 0);
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_PIXEL_BITWIDTH_MINUS1(index, frame),
					0 << 24 | 0 << 16 | 7 << 8 | 7 << 0);
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_PIXEL_BPP(index, frame),
					0 << 28 | 0 << 24 |
					0 << 20 | 0 << 16 |
					3 << 12 | 0 << 8 |
					3 << 4  | 0 << 0);

			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_STRIDE_COMP0(index, frame),
					pix->plane_fmt[0].bytesperline);
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_STRIDE_COMP1(index, frame),
					pix->plane_fmt[1].bytesperline);
			break;
		case V4L2_PIX_FMT_XBGR32:
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_MODE(index, frame),
					0);
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_PIXEL_POSITION(index, frame),
					0 << 24 | 0 << 16 | 0 << 8 | 0 << 0);
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_PIXEL_BITWIDTH_MINUS1(index, frame),
					0 << 24 | 0 << 16 | 0 << 8 | 23 << 0);
			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_PIXEL_BPP(index, frame),
					0 << 28 | 0 << 24 |
					0 << 20 | 0 << 16 |
					0 << 12 | 0 << 8 |
					3 << 4  | 2 << 0);

			risp_core_write(core,
					ISPCORE_DMA_IMAGE_FRAME_STRIDE_COMP0(index, frame),
					pix->plane_fmt[0].bytesperline);
			break;
		default:
			return -EINVAL;
		}

		risp_core_write(core,
				ISPCORE_DMA_IMAGE_FRAME_AXI_ID(index, frame),
				0);
	}

	/* Set image out flush EOF. */
	risp_core_write(core, ISPCORE_DMA_IMAGE_FLUSH_OUT_REG(index),
			pix->plane_fmt[0].bytesperline <<
			ISPCORE_DMA_IMAGE_FLUSH_OUT_PADDING_PIXEL_EOF_SHIFT);

	/* Enable DMA and set burst length. */
	risp_core_write(core, ISPCORE_DMA_IMAGE_AXI_CONFIG_REG(index),
			BIT(31) | 7);

	return 0;
}

static u32 risp_core_pix2bus(const struct rcar_isp_core_io *io)
{
	switch (io->format.fmt.pix_mp.pixelformat) {
	case V4L2_PIX_FMT_SBGGR8:
		return MEDIA_BUS_FMT_SBGGR8_1X8;
	case V4L2_PIX_FMT_SGBRG8:
		return MEDIA_BUS_FMT_SGBRG8_1X8;
	case V4L2_PIX_FMT_SGRBG8:
		return MEDIA_BUS_FMT_SGRBG8_1X8;
	case V4L2_PIX_FMT_SRGGB8:
		return MEDIA_BUS_FMT_SRGGB8_1X8;
	case V4L2_PIX_FMT_SBGGR10:
		return MEDIA_BUS_FMT_SBGGR10_1X10;
	case V4L2_PIX_FMT_SGBRG10:
		return MEDIA_BUS_FMT_SGBRG10_1X10;
	case V4L2_PIX_FMT_SGRBG10:
		return MEDIA_BUS_FMT_SGRBG10_1X10;
	case V4L2_PIX_FMT_SRGGB10:
		return MEDIA_BUS_FMT_SRGGB10_1X10;
	case V4L2_PIX_FMT_SBGGR12:
		return MEDIA_BUS_FMT_SBGGR12_1X12;
	case V4L2_PIX_FMT_SGBRG12:
		return MEDIA_BUS_FMT_SGBRG12_1X12;
	case V4L2_PIX_FMT_SGRBG12:
		return MEDIA_BUS_FMT_SGRBG12_1X12;
	case V4L2_PIX_FMT_SRGGB12:
		return MEDIA_BUS_FMT_SRGGB12_1X12;
	case V4L2_PIX_FMT_XBGR32:
		return MEDIA_BUS_FMT_RGB888_1X24;
	case V4L2_PIX_FMT_NV16M:
		return MEDIA_BUS_FMT_YUYV12_1X24;
	default:
		return 0;
	}
}

static void risp_core_try_next_job(struct rcar_isp_core *core)
{
	lockdep_assert_held(&core->lock);

	struct rcar_isp_job *job = core->vspx.job;

	/* If the ISP or the VSPX is not done with the job, wait. */
	if (!job || !job->done_isp || !job->done_vspx)
		return;

	core->vspx.job = NULL;
	kfree(job);

	core->sequence++;

	/* Kickoff processing of next frame (if any). */
	risp_core_job_run(core);
}

static void risp_core_vspx_frame_end(void *data)
{
	struct rcar_isp_core *core = data;

	guard(spinlock_irqsave)(&core->lock);

	/*
	 * In tear-down the ISP may report a frame end event but we have already
	 * freed the job. It is safe to ignore the end of frame event.
	 */
	if (!core->vspx.job)
		return;

	core->vspx.job->done_vspx = true;
	risp_core_try_next_job(core);
}

static int risp_core_power_on(struct rcar_isp_core *core)
{
	int ret;

	ret = pm_runtime_resume_and_get(core->dev);
	if (ret < 0)
		return ret;

	ret = reset_control_deassert(core->csrstc);
	if (ret)
		goto err_pm;

	ret = clk_prepare_enable(core->clk);
	if (ret)
		goto err_csrstc;

	return 0;

err_csrstc:
	reset_control_assert(core->csrstc);
err_pm:
	pm_runtime_put(core->dev);

	return ret;
}

static void risp_core_power_off(struct rcar_isp_core *core)
{
	clk_disable_unprepare(core->clk);

	reset_control_assert(core->csrstc);

	pm_runtime_put(core->dev);
}

int risp_core_start_streaming(struct rcar_isp_core *core)
{
	struct vsp1_vspx_frame_end vspx_fe = {
		.vspx_frame_end = risp_core_vspx_frame_end,
		.frame_end_data = core,
	};

	struct v4l2_mbus_framefmt inputfmt = {
		.width = core->io[RISP_CORE_INPUT1].format.fmt.pix_mp.width,
		.height = core->io[RISP_CORE_INPUT1].format.fmt.pix_mp.height,
		.code = risp_core_pix2bus(&core->io[RISP_CORE_INPUT1]),
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_RAW,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_FULL_RANGE,
		.xfer_func = V4L2_XFER_FUNC_NONE,
	};

	struct v4l2_mbus_framefmt hvout = {
		.width = core->io[RISP_CORE_OUTPUT1].format.fmt.pix_mp.width,
		.height = core->io[RISP_CORE_OUTPUT1].format.fmt.pix_mp.height,
		.code = risp_core_pix2bus(&core->io[RISP_CORE_OUTPUT1]),
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization =
			core->io[RISP_CORE_OUTPUT1].format.fmt.pix_mp.pixelformat ==
			V4L2_PIX_FMT_XBGR32 ?
			V4L2_QUANTIZATION_FULL_RANGE :
			V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_SRGB,
	};
	int ret;

	scoped_guard(mutex, &core->io_lock) {
		for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
			if (!core->io[i].streaming)
				return 0;
		}

		/*
		 * The state core->streaming is protected by core->lock, which
		 * is not held yet. It is however safe to read it here since
		 * core->io_lock is held both in risp_core_stop_streaming() and
		 * here, the only two places the variable is modified.
		 *
		 * With this small implied dependency on the two locks for write
		 * access, the interrupt handler can safely depend sole on the
		 * spinlock core->lock for read access to core->streaming.
		 *
		 * The gain is an interrupt handler which can hold the spinlock
		 * and a start/stop procedure which can reset the ISP using the
		 * reset_control_reset() API, The later which can not be called
		 * from a context that may sleep.
		 *
		 * All other locations core->streaming is read and _all_
		 * locations where it is written core->lock is held.
		 */
		if (core->streaming)
			return 0;

		ret = risp_core_power_on(core);
		if (ret)
			return ret;

		/* Reset and wait for ISP core to initialize itself. */
		reset_control_reset(core->rstc);
		usleep_range(2000, 4000);

		scoped_guard(spinlock_irqsave, &core->lock) {
			risp_core_write(core, ISP_CORE_ISPCORE_INT_ENABLE, 1);

			/* Configure output DMA */
			risp_core_config_output(core, 0,
						&core->io[RISP_CORE_OUTPUT1].format.fmt.pix_mp);

			risp_cs_write(core, ISP_CS_STREAMER_VBLANK_REG, inputfmt.width * 25);
			risp_cs_write(core, ISP_CS_STREAMER_HBLANK_REG, 64);

			/* Enable ISP Streaming bridge. */
			risp_cs_write(core, ISP_CS_STREAMER_MODE_REG,
				      ISP_CS_STREAMER_MODE_STREAMER_EN);

			/* Start RPP ISP */
			ret = rppx1_start(core->rpp, &inputfmt, &hvout, NULL);
			if (ret) {
				risp_core_power_off(core);
				return ret;
			}

			core->vspx.job = NULL;
			core->sequence = 0;
			core->streaming = true;
		}

		/* Start VSPX */
		vsp1_isp_start_streaming(core->vspx.dev, &vspx_fe);

		scoped_guard(spinlock_irqsave, &core->lock) {
			risp_core_job_run(core);
		}
	}

	return 0;
}

void risp_core_stop_streaming(struct rcar_isp_core *core)
{
	struct rcar_isp_job *job, *tmp;

	/*
	 * This function releases buffers and jobs: make sure the queues mutex
	 * is held.
	 */
	lockdep_assert_held(&core->io_lock);

	scoped_guard(spinlock_irqsave, &core->lock) {
		/* Stop is called by each vdev, only act on the first call. */
		if (!core->streaming)
			return;

		/* Stop queueing jobs to VSPX. */
		core->streaming = false;
	}

	/* Wait for active VSPX job to finish. */
	for (unsigned int retry = 0; retry <= 10; retry++) {
		if (!core->vspx.job)
			break;

		usleep_range(2000, 4000);
	}

	if (core->vspx.job)
		dev_err(core->dev, "Failed to complete running job");

	/* Free all buffers and switch off the hardware. */
	scoped_guard(spinlock_irqsave, &core->lock) {
		/* Free all jobs and buffers. */
		list_for_each_entry_safe(job, tmp, &core->risp_jobs, job_queue) {
			vsp1_isp_job_release(core->vspx.dev, &job->vspx_job);

			for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
				struct risp_buffer *buf = job->buffers[i];

				vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			}

			list_del(&job->job_queue);
			kfree(job);
		}

		rppx1_stop(core->rpp);
		risp_cs_write(core, ISP_CS_STREAMER_MODE_REG, 0);
		risp_core_write(core, ISP_CORE_ISPCORE_INT_ENABLE, 0);
	}

	vsp1_isp_stop_streaming(core->vspx.dev);

	risp_core_power_off(core);
}

static irqreturn_t risp_core_irq(int irq, void *data)
{
	struct rcar_isp_core *core = data;
	struct rcar_isp_job *job;
	u32 status;

	status = risp_core_read(core, ISP_CORE_ISPCORE_INT_STATUS);
	if (!(status & BIT(0)))
		return IRQ_NONE;

	if (!rppx1_interrupt(core->rpp, &status))
		return IRQ_HANDLED;

	guard(spinlock_irqsave)(&core->lock);

	job = core->vspx.job;
	if (!job)
		return IRQ_HANDLED;

	for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
		struct risp_buffer *buf;

		buf = job->buffers[i];

		switch (i) {
		case RISP_CORE_STATS:
			rppx1_stats_fill_isr(core->rpp, status,
					     vb2_plane_vaddr(&buf->vb.vb2_buf, 0));
			fallthrough;
		case RISP_CORE_OUTPUT1:
		case RISP_CORE_INPUT1:
			buf->vb.sequence = core->sequence;
			buf->vb.vb2_buf.timestamp = ktime_get_ns();
			fallthrough;
		case RISP_CORE_PARAMS:
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
			break;
		}
	}

	core->vspx.job->done_isp = true;
	risp_core_try_next_job(core);

	return IRQ_HANDLED;
}

static const struct v4l2_subdev_ops risp_core_subdev_ops = {
};

static int risp_core_create_subdev(struct rcar_isp_core *core)
{
	struct v4l2_subdev *subdev = &core->subdev;
	int ret;

	subdev->owner = THIS_MODULE;
	subdev->dev = core->dev;
	v4l2_subdev_init(subdev, &risp_core_subdev_ops);
	v4l2_set_subdevdata(subdev, core->dev);
	snprintf(subdev->name, sizeof(subdev->name), "%s %s core",
		 KBUILD_MODNAME, dev_name(core->dev));
	subdev->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	subdev->entity.function = MEDIA_ENT_F_VID_MUX;

	core->pads[RISP_CORE_INPUT1].flags = MEDIA_PAD_FL_SINK;
	core->pads[RISP_CORE_PARAMS].flags = MEDIA_PAD_FL_SINK;
	core->pads[RISP_CORE_STATS].flags = MEDIA_PAD_FL_SOURCE;
	core->pads[RISP_CORE_OUTPUT1].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&subdev->entity, RISP_CORE_NUM_PADS,
				     core->pads);
	if (ret)
		return ret;

	return 0;
}

int risp_core_registered(struct rcar_isp_core *core, struct v4l2_subdev *sd)
{
	int ret;

	core->v4l2_dev.mdev = sd->v4l2_dev->mdev;

	/* Register ISP Core subdevice. */
	ret = v4l2_device_register_subdev(&core->v4l2_dev, &core->subdev);
	if (ret)
		return ret;

	for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++) {
		ret = risp_core_io_create(core->dev, core, &core->io[i], i);
		if (ret) {
			/* It is safe to destroy io node that is not created. */
			for (unsigned int n = 0; n < RISP_CORE_NUM_PADS; n++)
				risp_core_io_destroy(&core->io[n]);

			v4l2_device_unregister_subdev(&core->subdev);

			return ret;
		}
	}

	return 0;
}

static int risp_core_probe_resources(struct rcar_isp_core *core,
				     struct platform_device *pdev)
{
	struct platform_device *vspx;
	struct device_node *of_vspx;
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core");
	if (!res)
		return -ENODEV;

	core->rppaddr = res->start;
	core->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(core->base))
		return PTR_ERR(core->base);

	ret = platform_get_irq_byname(pdev, "core");
	if (ret < 0)
		return -ENODEV;

	ret = devm_request_irq(&pdev->dev, ret, risp_core_irq, IRQF_SHARED,
			       KBUILD_MODNAME, core);
	if (ret)
		return ret;

	core->clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(core->clk))
		return -ENODEV;

	core->rstc = devm_reset_control_get(&pdev->dev, "core");
	if (IS_ERR(core->rstc))
		return -ENODEV;

	of_vspx = of_parse_phandle(pdev->dev.of_node, "renesas,vspx", 0);
	if (!of_vspx)
		return -ENODEV;

	vspx = of_find_device_by_node(of_vspx);
	of_node_put(of_vspx);
	if (!vspx)
		return -ENODEV;

	/* Attach to VSP-X */
	core->vspx.dev = &vspx->dev;

	ret = vsp1_isp_init(&vspx->dev);
	if (ret < 0)
		goto err_put_vspx;

	/* Attach to the RPP library
	 *
	 * 1. Start and wait for the ISP to startup.
	 * 2. Attach the RPP library and talk with the RPP ISP.
	 * 3. Turn off ISP.
	 * 4. Fail if the RPP is unhappy with the hardware.
	 */
	ret = clk_prepare_enable(core->clk);
	if (ret)
		goto err_put_vspx;

	usleep_range(2000, 4000);

	core->rpp = rppx1_create(core->base, &pdev->dev);

	clk_disable_unprepare(core->clk);

	if (!core->rpp) {
		ret = -ENODEV;
		goto err_put_vspx;
	}

	return 0;

err_put_vspx:
	put_device(&vspx->dev);
	return ret;
}

int risp_core_probe(struct rcar_isp_core *core, struct platform_device *pdev,
		    void __iomem *csbase, struct reset_control *csrstc)
{
	int ret;

	core->dev = &pdev->dev;
	core->csrstc = csrstc;
	core->csbase = csbase;

	ret = risp_core_probe_resources(core, pdev);
	if (ret) {
		core->base = NULL;
		return ret;
	}

	ret = v4l2_device_register(core->dev, &core->v4l2_dev);
	if (ret)
		goto err_destroy_rpp;

	ret = risp_core_create_subdev(core);
	if (ret)
		goto err_unregister_v4l2;

	mutex_init(&core->io_lock);
	spin_lock_init(&core->lock);
	INIT_LIST_HEAD(&core->risp_jobs);

	return 0;

err_unregister_v4l2:
	v4l2_device_unregister(&core->v4l2_dev);
err_destroy_rpp:
	rppx1_destroy(core->rpp);
	put_device(core->vspx.dev);
	return ret;
}

void risp_core_remove(struct rcar_isp_core *core)
{
	/* If we did not probe the ISP core, nothing to do. */
	if (!core->base)
		return;

	dev_info(core->dev, "Remove ISP Core\n");

	for (unsigned int i = 0; i < RISP_CORE_NUM_PADS; i++)
		risp_core_io_destroy(&core->io[i]);

	v4l2_device_unregister(&core->v4l2_dev);

	mutex_destroy(&core->io_lock);
	rppx1_destroy(core->rpp);
	put_device(core->vspx.dev);
}
