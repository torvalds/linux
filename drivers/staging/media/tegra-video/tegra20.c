// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra20-specific VI implementation
 *
 * Copyright (C) 2023 SKIDATA GmbH
 * Author: Luca Ceresoli <luca.ceresoli@bootlin.com>
 */

/*
 * This source file contains Tegra20 supported video formats,
 * VI and VIP SoC specific data, operations and registers accessors.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/host1x.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/v4l2-mediabus.h>

#include "vip.h"
#include "vi.h"

#define TEGRA_VI_SYNCPT_WAIT_TIMEOUT			msecs_to_jiffies(200)

/* This are just good-sense numbers. The actual min/max is not documented. */
#define TEGRA20_MIN_WIDTH	32U
#define TEGRA20_MIN_HEIGHT	32U
#define TEGRA20_MAX_WIDTH	2048U
#define TEGRA20_MAX_HEIGHT	2048U

/* --------------------------------------------------------------------------
 * Registers
 */

#define TEGRA_VI_CONT_SYNCPT_OUT_1			0x0060
#define       VI_CONT_SYNCPT_OUT_1_CONTINUOUS_SYNCPT	BIT(8)
#define       VI_CONT_SYNCPT_OUT_1_SYNCPT_IDX_SFT	0

#define TEGRA_VI_VI_INPUT_CONTROL			0x0088
#define       VI_INPUT_FIELD_DETECT			BIT(27)
#define       VI_INPUT_BT656				BIT(25)
#define       VI_INPUT_YUV_INPUT_FORMAT_SFT		8  /* bits [9:8] */
#define       VI_INPUT_YUV_INPUT_FORMAT_UYVY		(0 << VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_YUV_INPUT_FORMAT_VYUY		BIT(VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_YUV_INPUT_FORMAT_YUYV		(2 << VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_YUV_INPUT_FORMAT_YVYU		(3 << VI_INPUT_YUV_INPUT_FORMAT_SFT)
#define       VI_INPUT_INPUT_FORMAT_SFT			2  /* bits [5:2] */
#define       VI_INPUT_INPUT_FORMAT_YUV422		(0 << VI_INPUT_INPUT_FORMAT_SFT)
#define       VI_INPUT_VIP_INPUT_ENABLE			BIT(1)

#define TEGRA_VI_VI_CORE_CONTROL			0x008c
#define       VI_VI_CORE_CONTROL_PLANAR_CONV_IN_SEL_EXT	BIT(31)
#define       VI_VI_CORE_CONTROL_CSC_INPUT_SEL_EXT	BIT(30)
#define       VI_VI_CORE_CONTROL_INPUT_TO_ALT_MUX_SFT	27
#define       VI_VI_CORE_CONTROL_INPUT_TO_CORE_EXT_SFT	24
#define       VI_VI_CORE_CONTROL_OUTPUT_TO_ISP_EXT_SFT	21
#define       VI_VI_CORE_CONTROL_ISP_HOST_STALL_OFF	BIT(20)
#define       VI_VI_CORE_CONTROL_V_DOWNSCALING		BIT(19)
#define       VI_VI_CORE_CONTROL_V_AVERAGING		BIT(18)
#define       VI_VI_CORE_CONTROL_H_DOWNSCALING		BIT(17)
#define       VI_VI_CORE_CONTROL_H_AVERAGING		BIT(16)
#define       VI_VI_CORE_CONTROL_CSC_INPUT_SEL		BIT(11)
#define       VI_VI_CORE_CONTROL_PLANAR_CONV_INPUT_SEL	BIT(10)
#define       VI_VI_CORE_CONTROL_INPUT_TO_CORE_SFT	8
#define       VI_VI_CORE_CONTROL_ISP_DOWNSAMPLE_SFT	5
#define       VI_VI_CORE_CONTROL_OUTPUT_TO_EPP_SFT	2
#define       VI_VI_CORE_CONTROL_OUTPUT_TO_ISP_SFT	0

#define TEGRA_VI_VI_FIRST_OUTPUT_CONTROL		0x0090
#define       VI_OUTPUT_FORMAT_EXT			BIT(22)
#define       VI_OUTPUT_V_DIRECTION			BIT(20)
#define       VI_OUTPUT_H_DIRECTION			BIT(19)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT		17
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_UYVY		(0 << VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_VYUY		BIT(VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_YUYV		(2 << VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_YUV_OUTPUT_FORMAT_YVYU		(3 << VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_OUTPUT_BYTE_SWAP		BIT(16)
#define       VI_OUTPUT_LAST_PIXEL_DUPLICATION		BIT(8)
#define       VI_OUTPUT_OUTPUT_FORMAT_SFT		0
#define       VI_OUTPUT_OUTPUT_FORMAT_YUV422POST	(3 << VI_OUTPUT_OUTPUT_FORMAT_SFT)
#define       VI_OUTPUT_OUTPUT_FORMAT_YUV420PLANAR	(6 << VI_OUTPUT_OUTPUT_FORMAT_SFT)

#define TEGRA_VI_VIP_H_ACTIVE				0x00a4
#define       VI_VIP_H_ACTIVE_PERIOD_SFT		16 /* active pixels/line, must be even */
#define       VI_VIP_H_ACTIVE_START_SFT			0

#define TEGRA_VI_VIP_V_ACTIVE				0x00a8
#define       VI_VIP_V_ACTIVE_PERIOD_SFT		16 /* active lines */
#define       VI_VIP_V_ACTIVE_START_SFT			0

#define TEGRA_VI_VB0_START_ADDRESS_FIRST		0x00c4
#define TEGRA_VI_VB0_BASE_ADDRESS_FIRST			0x00c8
#define TEGRA_VI_VB0_START_ADDRESS_U			0x00cc
#define TEGRA_VI_VB0_BASE_ADDRESS_U			0x00d0
#define TEGRA_VI_VB0_START_ADDRESS_V			0x00d4
#define TEGRA_VI_VB0_BASE_ADDRESS_V			0x00d8

#define TEGRA_VI_FIRST_OUTPUT_FRAME_SIZE		0x00e0
#define       VI_FIRST_OUTPUT_FRAME_HEIGHT_SFT		16
#define       VI_FIRST_OUTPUT_FRAME_WIDTH_SFT		0

#define TEGRA_VI_VB0_COUNT_FIRST			0x00e4

#define TEGRA_VI_VB0_SIZE_FIRST				0x00e8
#define       VI_VB0_SIZE_FIRST_V_SFT			16
#define       VI_VB0_SIZE_FIRST_H_SFT			0

#define TEGRA_VI_VB0_BUFFER_STRIDE_FIRST		0x00ec
#define       VI_VB0_BUFFER_STRIDE_FIRST_CHROMA_SFT	30
#define       VI_VB0_BUFFER_STRIDE_FIRST_LUMA_SFT	0

#define TEGRA_VI_H_LPF_CONTROL				0x0108
#define       VI_H_LPF_CONTROL_CHROMA_SFT		16
#define       VI_H_LPF_CONTROL_LUMA_SFT			0

#define TEGRA_VI_H_DOWNSCALE_CONTROL			0x010c
#define TEGRA_VI_V_DOWNSCALE_CONTROL			0x0110

#define TEGRA_VI_VIP_INPUT_STATUS			0x0144

#define TEGRA_VI_VI_DATA_INPUT_CONTROL			0x0168
#define       VI_DATA_INPUT_SFT				0 /* [11:0] = mask pin inputs to VI core */

#define TEGRA_VI_PIN_INPUT_ENABLE			0x016c
#define       VI_PIN_INPUT_VSYNC			BIT(14)
#define       VI_PIN_INPUT_HSYNC			BIT(13)
#define       VI_PIN_INPUT_VD_SFT			0 /* [11:0] = data bin N input enable */

#define TEGRA_VI_PIN_INVERSION				0x0174
#define       VI_PIN_INVERSION_VSYNC_ACTIVE_HIGH	BIT(1)
#define       VI_PIN_INVERSION_HSYNC_ACTIVE_HIGH	BIT(0)

#define TEGRA_VI_CAMERA_CONTROL				0x01a0
#define       VI_CAMERA_CONTROL_STOP_CAPTURE		BIT(2)
#define       VI_CAMERA_CONTROL_TEST_MODE		BIT(1)
#define       VI_CAMERA_CONTROL_VIP_ENABLE		BIT(0)

#define TEGRA_VI_VI_ENABLE				0x01a4
#define       VI_VI_ENABLE_SW_FLOW_CONTROL_OUT1		BIT(1)
#define       VI_VI_ENABLE_FIRST_OUTPUT_TO_MEM_DISABLE	BIT(0)

#define TEGRA_VI_VI_RAISE				0x01ac
#define       VI_VI_RAISE_ON_EDGE			BIT(0)

/* --------------------------------------------------------------------------
 * VI
 */

static void tegra20_vi_write(struct tegra_vi_channel *chan, unsigned int addr, u32 val)
{
	writel(val, chan->vi->iomem + addr);
}

/*
 * Get the main input format (YUV/RGB...) and the YUV variant as values to
 * be written into registers for the current VI input mbus code.
 */
static void tegra20_vi_get_input_formats(struct tegra_vi_channel *chan,
					 unsigned int *main_input_format,
					 unsigned int *yuv_input_format)
{
	unsigned int input_mbus_code = chan->fmtinfo->code;

	(*main_input_format) = VI_INPUT_INPUT_FORMAT_YUV422;
	(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_UYVY;

	switch (input_mbus_code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_UYVY;
		break;
	case MEDIA_BUS_FMT_VYUY8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_VYUY;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_YUYV;
		break;
	case MEDIA_BUS_FMT_YVYU8_2X8:
		(*yuv_input_format) = VI_INPUT_YUV_INPUT_FORMAT_YVYU;
		break;
	}
}

/*
 * Get the main output format (YUV/RGB...) and the YUV variant as values to
 * be written into registers for the current VI output pixel format.
 */
static void tegra20_vi_get_output_formats(struct tegra_vi_channel *chan,
					  unsigned int *main_output_format,
					  unsigned int *yuv_output_format)
{
	u32 output_fourcc = chan->format.pixelformat;

	/* Default to YUV422 non-planar (U8Y8V8Y8) after downscaling */
	(*main_output_format) = VI_OUTPUT_OUTPUT_FORMAT_YUV422POST;
	(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_UYVY;

	switch (output_fourcc) {
	case V4L2_PIX_FMT_UYVY:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_UYVY;
		break;
	case V4L2_PIX_FMT_VYUY:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_VYUY;
		break;
	case V4L2_PIX_FMT_YUYV:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_YUYV;
		break;
	case V4L2_PIX_FMT_YVYU:
		(*yuv_output_format) = VI_OUTPUT_YUV_OUTPUT_FORMAT_YVYU;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		(*main_output_format) = VI_OUTPUT_OUTPUT_FORMAT_YUV420PLANAR;
		break;
	}
}

/*
 * Make the VI accessible (needed on Tegra20).
 *
 * This function writes an unknown bit into an unknown register. The code
 * comes from a downstream 3.1 kernel that has a working VIP driver for
 * Tegra20, and removing it makes the VI completely unaccessible. It should
 * be rewritten and possibly moved elsewhere, but the appropriate location
 * and implementation is unknown due to a total lack of documentation.
 */
static int tegra20_vi_enable(struct tegra_vi *vi, bool on)
{
	/* from arch/arm/mach-tegra/iomap.h */
	const phys_addr_t TEGRA_APB_MISC_BASE = 0x70000000;
	const unsigned long reg_offset = 0x42c;
	void __iomem *apb_misc;
	u32 val;

	apb_misc = ioremap(TEGRA_APB_MISC_BASE, PAGE_SIZE);
	if (!apb_misc)
		apb_misc = ERR_PTR(-ENOENT);
	if (IS_ERR(apb_misc))
		return dev_err_probe(vi->dev, PTR_ERR(apb_misc), "cannot access APB_MISC");

	val = readl(apb_misc + reg_offset);
	val &= ~BIT(0);
	val |= on ? BIT(0) : 0;
	writel(val, apb_misc + reg_offset);
	iounmap(apb_misc);

	return 0;
}

static int tegra20_channel_host1x_syncpt_init(struct tegra_vi_channel *chan)
{
	struct tegra_vi *vi = chan->vi;
	struct host1x_syncpt *out_sp;

	out_sp = host1x_syncpt_request(&vi->client, HOST1X_SYNCPT_CLIENT_MANAGED);
	if (!out_sp)
		return dev_err_probe(vi->dev, -ENOMEM, "failed to request syncpoint\n");

	chan->mw_ack_sp[0] = out_sp;

	return 0;
}

static void tegra20_channel_host1x_syncpt_free(struct tegra_vi_channel *chan)
{
	host1x_syncpt_put(chan->mw_ack_sp[0]);
}

static void tegra20_fmt_align(struct v4l2_pix_format *pix, unsigned int bpp)
{
	pix->width  = clamp(pix->width,  TEGRA20_MIN_WIDTH,  TEGRA20_MAX_WIDTH);
	pix->height = clamp(pix->height, TEGRA20_MIN_HEIGHT, TEGRA20_MAX_HEIGHT);

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		pix->bytesperline = roundup(pix->width, 2) * 2;
		pix->sizeimage = roundup(pix->width, 2) * 2 * pix->height;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		pix->bytesperline = roundup(pix->width, 8);
		pix->sizeimage = roundup(pix->width, 8) * pix->height * 3 / 2;
		break;
	}
}

/*
 * Compute buffer offsets once per stream so that
 * tegra20_channel_vi_buffer_setup() only has to do very simple maths for
 * each buffer.
 */
static void tegra20_channel_queue_setup(struct tegra_vi_channel *chan)
{
	unsigned int stride = chan->format.bytesperline;
	unsigned int height = chan->format.height;

	chan->start_offset = 0;

	switch (chan->format.pixelformat) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		if (chan->vflip)
			chan->start_offset += stride * (height - 1);
		if (chan->hflip)
			chan->start_offset += stride - 1;
		break;

	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		chan->addr_offset_u = stride * height;
		chan->addr_offset_v = chan->addr_offset_u + stride * height / 4;

		/* For YVU420, we swap the locations of the U and V planes. */
		if (chan->format.pixelformat == V4L2_PIX_FMT_YVU420)
			swap(chan->addr_offset_u, chan->addr_offset_v);

		chan->start_offset_u = chan->addr_offset_u;
		chan->start_offset_v = chan->addr_offset_v;

		if (chan->vflip) {
			chan->start_offset   += stride * (height - 1);
			chan->start_offset_u += (stride / 2) * ((height / 2) - 1);
			chan->start_offset_v += (stride / 2) * ((height / 2) - 1);
		}
		if (chan->hflip) {
			chan->start_offset   += stride - 1;
			chan->start_offset_u += (stride / 2) - 1;
			chan->start_offset_v += (stride / 2) - 1;
		}
		break;
	}
}

static void release_buffer(struct tegra_vi_channel *chan,
			   struct tegra_channel_buffer *buf,
			   enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *vb = &buf->buf;

	vb->sequence = chan->sequence++;
	vb->field = V4L2_FIELD_NONE;
	vb->vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&vb->vb2_buf, state);
}

static void tegra20_channel_vi_buffer_setup(struct tegra_vi_channel *chan,
					    struct tegra_channel_buffer *buf)
{
	dma_addr_t base = buf->addr;

	switch (chan->fmtinfo->fourcc) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		tegra20_vi_write(chan, TEGRA_VI_VB0_BASE_ADDRESS_U,  base + chan->addr_offset_u);
		tegra20_vi_write(chan, TEGRA_VI_VB0_START_ADDRESS_U, base + chan->start_offset_u);
		tegra20_vi_write(chan, TEGRA_VI_VB0_BASE_ADDRESS_V,  base + chan->addr_offset_v);
		tegra20_vi_write(chan, TEGRA_VI_VB0_START_ADDRESS_V, base + chan->start_offset_v);
		fallthrough;

	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		tegra20_vi_write(chan, TEGRA_VI_VB0_BASE_ADDRESS_FIRST,  base);
		tegra20_vi_write(chan, TEGRA_VI_VB0_START_ADDRESS_FIRST, base + chan->start_offset);
		break;
	}
}

static int tegra20_channel_capture_frame(struct tegra_vi_channel *chan,
					 struct tegra_channel_buffer *buf)
{
	int err;

	chan->next_out_sp_idx++;

	tegra20_channel_vi_buffer_setup(chan, buf);

	tegra20_vi_write(chan, TEGRA_VI_CAMERA_CONTROL, VI_CAMERA_CONTROL_VIP_ENABLE);

	/* Wait for syncpt counter to reach frame start event threshold */
	err = host1x_syncpt_wait(chan->mw_ack_sp[0], chan->next_out_sp_idx,
				 TEGRA_VI_SYNCPT_WAIT_TIMEOUT, NULL);
	if (err) {
		host1x_syncpt_incr(chan->mw_ack_sp[0]);
		dev_err_ratelimited(&chan->video.dev, "frame start syncpt timeout: %d\n", err);
		release_buffer(chan, buf, VB2_BUF_STATE_ERROR);
		return err;
	}

	tegra20_vi_write(chan, TEGRA_VI_CAMERA_CONTROL,
			 VI_CAMERA_CONTROL_STOP_CAPTURE | VI_CAMERA_CONTROL_VIP_ENABLE);

	release_buffer(chan, buf, VB2_BUF_STATE_DONE);

	return 0;
}

static int tegra20_chan_capture_kthread_start(void *data)
{
	struct tegra_vi_channel *chan = data;
	struct tegra_channel_buffer *buf;
	unsigned int retries = 0;
	int err = 0;

	while (1) {
		/*
		 * Source is not streaming if error is non-zero.
		 * So, do not dequeue buffers on error and let the thread sleep
		 * till kthread stop signal is received.
		 */
		wait_event_interruptible(chan->start_wait,
					 kthread_should_stop() ||
					 (!list_empty(&chan->capture) && !err));

		if (kthread_should_stop())
			break;

		/* dequeue the buffer and start capture */
		spin_lock(&chan->start_lock);
		if (list_empty(&chan->capture)) {
			spin_unlock(&chan->start_lock);
			continue;
		}

		buf = list_first_entry(&chan->capture, struct tegra_channel_buffer, queue);
		list_del_init(&buf->queue);
		spin_unlock(&chan->start_lock);

		err = tegra20_channel_capture_frame(chan, buf);
		if (!err) {
			retries = 0;
			continue;
		}

		if (retries++ > chan->syncpt_timeout_retry)
			vb2_queue_error(&chan->queue);
		else
			err = 0;
	}

	return 0;
}

static void tegra20_camera_capture_setup(struct tegra_vi_channel *chan)
{
	u32 output_fourcc = chan->format.pixelformat;
	int width  = chan->format.width;
	int height = chan->format.height;
	int stride_l = chan->format.bytesperline;
	int stride_c = (output_fourcc == V4L2_PIX_FMT_YUV420 ||
			output_fourcc == V4L2_PIX_FMT_YVU420) ? 1 : 0;
	int main_output_format;
	int yuv_output_format;

	tegra20_vi_get_output_formats(chan, &main_output_format, &yuv_output_format);

	/*
	 * Set up low pass filter.  Use 0x240 for chromaticity and 0x240
	 * for luminance, which is the default and means not to touch
	 * anything.
	 */
	tegra20_vi_write(chan, TEGRA_VI_H_LPF_CONTROL,
			 0x0240 << VI_H_LPF_CONTROL_LUMA_SFT |
			 0x0240 << VI_H_LPF_CONTROL_CHROMA_SFT);

	/* Set up raise-on-edge, so we get an interrupt on end of frame. */
	tegra20_vi_write(chan, TEGRA_VI_VI_RAISE, VI_VI_RAISE_ON_EDGE);

	tegra20_vi_write(chan, TEGRA_VI_VI_FIRST_OUTPUT_CONTROL,
			 (chan->vflip ? VI_OUTPUT_V_DIRECTION : 0) |
			 (chan->hflip ? VI_OUTPUT_H_DIRECTION : 0) |
			 yuv_output_format << VI_OUTPUT_YUV_OUTPUT_FORMAT_SFT |
			 main_output_format << VI_OUTPUT_OUTPUT_FORMAT_SFT);

	/* Set up frame size */
	tegra20_vi_write(chan, TEGRA_VI_FIRST_OUTPUT_FRAME_SIZE,
			 height << VI_FIRST_OUTPUT_FRAME_HEIGHT_SFT |
			 width  << VI_FIRST_OUTPUT_FRAME_WIDTH_SFT);

	/* First output memory enabled */
	tegra20_vi_write(chan, TEGRA_VI_VI_ENABLE, 0);

	/* Set the number of frames in the buffer */
	tegra20_vi_write(chan, TEGRA_VI_VB0_COUNT_FIRST, 1);

	/* Set up buffer frame size */
	tegra20_vi_write(chan, TEGRA_VI_VB0_SIZE_FIRST,
			 height << VI_VB0_SIZE_FIRST_V_SFT |
			 width  << VI_VB0_SIZE_FIRST_H_SFT);

	tegra20_vi_write(chan, TEGRA_VI_VB0_BUFFER_STRIDE_FIRST,
			 stride_l << VI_VB0_BUFFER_STRIDE_FIRST_LUMA_SFT |
			 stride_c << VI_VB0_BUFFER_STRIDE_FIRST_CHROMA_SFT);

	tegra20_vi_write(chan, TEGRA_VI_VI_ENABLE, 0);
}

static int tegra20_vi_start_streaming(struct vb2_queue *vq, u32 count)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);
	struct media_pipeline *pipe = &chan->video.pipe;
	int err;

	chan->next_out_sp_idx = host1x_syncpt_read(chan->mw_ack_sp[0]);

	err = video_device_pipeline_start(&chan->video, pipe);
	if (err)
		goto error_pipeline_start;

	tegra20_camera_capture_setup(chan);

	err = tegra_channel_set_stream(chan, true);
	if (err)
		goto error_set_stream;

	chan->sequence = 0;

	chan->kthread_start_capture = kthread_run(tegra20_chan_capture_kthread_start,
						  chan, "%s:0", chan->video.name);
	if (IS_ERR(chan->kthread_start_capture)) {
		err = PTR_ERR(chan->kthread_start_capture);
		chan->kthread_start_capture = NULL;
		dev_err_probe(&chan->video.dev, err, "failed to run capture kthread\n");
		goto error_kthread_start;
	}

	return 0;

error_kthread_start:
	tegra_channel_set_stream(chan, false);
error_set_stream:
	video_device_pipeline_stop(&chan->video);
error_pipeline_start:
	tegra_channel_release_buffers(chan, VB2_BUF_STATE_QUEUED);

	return err;
}

static void tegra20_vi_stop_streaming(struct vb2_queue *vq)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);

	if (chan->kthread_start_capture) {
		kthread_stop(chan->kthread_start_capture);
		chan->kthread_start_capture = NULL;
	}

	tegra_channel_release_buffers(chan, VB2_BUF_STATE_ERROR);
	tegra_channel_set_stream(chan, false);
	video_device_pipeline_stop(&chan->video);
}

static const struct tegra_vi_ops tegra20_vi_ops = {
	.vi_enable = tegra20_vi_enable,
	.channel_host1x_syncpt_init = tegra20_channel_host1x_syncpt_init,
	.channel_host1x_syncpt_free = tegra20_channel_host1x_syncpt_free,
	.vi_fmt_align = tegra20_fmt_align,
	.channel_queue_setup = tegra20_channel_queue_setup,
	.vi_start_streaming = tegra20_vi_start_streaming,
	.vi_stop_streaming = tegra20_vi_stop_streaming,
};

#define TEGRA20_VIDEO_FMT(MBUS_CODE, BPP, FOURCC)	\
{							\
	.code    = MEDIA_BUS_FMT_##MBUS_CODE,		\
	.bpp     = BPP,					\
	.fourcc  = V4L2_PIX_FMT_##FOURCC,		\
}

static const struct tegra_video_format tegra20_video_formats[] = {
	TEGRA20_VIDEO_FMT(UYVY8_2X8, 2, UYVY),
	TEGRA20_VIDEO_FMT(VYUY8_2X8, 2, VYUY),
	TEGRA20_VIDEO_FMT(YUYV8_2X8, 2, YUYV),
	TEGRA20_VIDEO_FMT(YVYU8_2X8, 2, YVYU),
	TEGRA20_VIDEO_FMT(UYVY8_2X8, 1, YUV420),
	TEGRA20_VIDEO_FMT(UYVY8_2X8, 1, YVU420),
};

const struct tegra_vi_soc tegra20_vi_soc = {
	.video_formats = tegra20_video_formats,
	.nformats = ARRAY_SIZE(tegra20_video_formats),
	.default_video_format = &tegra20_video_formats[0],
	.ops = &tegra20_vi_ops,
	.vi_max_channels = 1, /* parallel input (VIP) */
	.vi_max_clk_hz = 150000000,
	.has_h_v_flip = true,
};

/* --------------------------------------------------------------------------
 * VIP
 */

/*
 * VIP-specific configuration for stream start.
 *
 * Whatever is common among VIP and CSI is done by the VI component (see
 * tegra20_vi_start_streaming()). Here we do what is VIP-specific.
 */
static int tegra20_vip_start_streaming(struct tegra_vip_channel *vip_chan)
{
	struct tegra_vi_channel *vi_chan = v4l2_get_subdev_hostdata(&vip_chan->subdev);
	int width  = vi_chan->format.width;
	int height = vi_chan->format.height;

	unsigned int main_input_format;
	unsigned int yuv_input_format;

	tegra20_vi_get_input_formats(vi_chan, &main_input_format, &yuv_input_format);

	tegra20_vi_write(vi_chan, TEGRA_VI_VI_CORE_CONTROL, 0);

	tegra20_vi_write(vi_chan, TEGRA_VI_VI_INPUT_CONTROL,
			 VI_INPUT_VIP_INPUT_ENABLE | main_input_format | yuv_input_format);

	tegra20_vi_write(vi_chan, TEGRA_VI_V_DOWNSCALE_CONTROL, 0);
	tegra20_vi_write(vi_chan, TEGRA_VI_H_DOWNSCALE_CONTROL, 0);

	tegra20_vi_write(vi_chan, TEGRA_VI_VIP_V_ACTIVE, height << VI_VIP_V_ACTIVE_PERIOD_SFT);
	tegra20_vi_write(vi_chan, TEGRA_VI_VIP_H_ACTIVE,
			 roundup(width, 2) << VI_VIP_H_ACTIVE_PERIOD_SFT);

	/*
	 * For VIP, D9..D2 is mapped to the video decoder's P7..P0.
	 * Disable/mask out the other Dn wires. When not in BT656
	 * mode we also need the V/H sync.
	 */
	tegra20_vi_write(vi_chan, TEGRA_VI_PIN_INPUT_ENABLE,
			 GENMASK(9, 2) << VI_PIN_INPUT_VD_SFT |
			 VI_PIN_INPUT_HSYNC | VI_PIN_INPUT_VSYNC);
	tegra20_vi_write(vi_chan, TEGRA_VI_VI_DATA_INPUT_CONTROL,
			 GENMASK(9, 2) << VI_DATA_INPUT_SFT);
	tegra20_vi_write(vi_chan, TEGRA_VI_PIN_INVERSION, 0);

	tegra20_vi_write(vi_chan, TEGRA_VI_CONT_SYNCPT_OUT_1,
			 VI_CONT_SYNCPT_OUT_1_CONTINUOUS_SYNCPT |
			 host1x_syncpt_id(vi_chan->mw_ack_sp[0])
			 << VI_CONT_SYNCPT_OUT_1_SYNCPT_IDX_SFT);

	tegra20_vi_write(vi_chan, TEGRA_VI_CAMERA_CONTROL, VI_CAMERA_CONTROL_STOP_CAPTURE);

	return 0;
}

static const struct tegra_vip_ops tegra20_vip_ops = {
	.vip_start_streaming = tegra20_vip_start_streaming,
};

const struct tegra_vip_soc tegra20_vip_soc = {
	.ops = &tegra20_vip_ops,
};
