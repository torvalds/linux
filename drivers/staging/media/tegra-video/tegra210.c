// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 NVIDIA CORPORATION.  All rights reserved.
 */

/*
 * This source file contains Tegra210 supported video formats,
 * VI and CSI SoC specific data, operations and registers accessors.
 */
#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/delay.h>
#include <linux/host1x.h>
#include <linux/kthread.h>

#include "csi.h"
#include "vi.h"

#define TEGRA_VI_SYNCPT_WAIT_TIMEOUT			msecs_to_jiffies(200)

/* Tegra210 VI registers */
#define TEGRA_VI_CFG_VI_INCR_SYNCPT			0x000
#define   VI_CFG_VI_INCR_SYNCPT_COND(x)			(((x) & 0xff) << 8)
#define   VI_CSI_PP_FRAME_START(port)			(5 + (port) * 4)
#define   VI_CSI_MW_ACK_DONE(port)			(7 + (port) * 4)
#define TEGRA_VI_CFG_VI_INCR_SYNCPT_CNTRL		0x004
#define   VI_INCR_SYNCPT_NO_STALL			BIT(8)
#define TEGRA_VI_CFG_VI_INCR_SYNCPT_ERROR		0x008
#define TEGRA_VI_CFG_CG_CTRL				0x0b8
#define   VI_CG_2ND_LEVEL_EN				0x1

/* Tegra210 VI CSI registers */
#define TEGRA_VI_CSI_SW_RESET				0x000
#define TEGRA_VI_CSI_SINGLE_SHOT			0x004
#define   SINGLE_SHOT_CAPTURE				0x1
#define TEGRA_VI_CSI_IMAGE_DEF				0x00c
#define   BYPASS_PXL_TRANSFORM_OFFSET			24
#define   IMAGE_DEF_FORMAT_OFFSET			16
#define   IMAGE_DEF_DEST_MEM				0x1
#define TEGRA_VI_CSI_IMAGE_SIZE				0x018
#define   IMAGE_SIZE_HEIGHT_OFFSET			16
#define TEGRA_VI_CSI_IMAGE_SIZE_WC			0x01c
#define TEGRA_VI_CSI_IMAGE_DT				0x020
#define TEGRA_VI_CSI_SURFACE0_OFFSET_MSB		0x024
#define TEGRA_VI_CSI_SURFACE0_OFFSET_LSB		0x028
#define TEGRA_VI_CSI_SURFACE1_OFFSET_MSB		0x02c
#define TEGRA_VI_CSI_SURFACE1_OFFSET_LSB		0x030
#define TEGRA_VI_CSI_SURFACE2_OFFSET_MSB		0x034
#define TEGRA_VI_CSI_SURFACE2_OFFSET_LSB		0x038
#define TEGRA_VI_CSI_SURFACE0_STRIDE			0x054
#define TEGRA_VI_CSI_SURFACE1_STRIDE			0x058
#define TEGRA_VI_CSI_SURFACE2_STRIDE			0x05c
#define TEGRA_VI_CSI_SURFACE_HEIGHT0			0x060
#define TEGRA_VI_CSI_ERROR_STATUS			0x084

/* Tegra210 CSI Pixel Parser registers: Starts from 0x838, offset 0x0 */
#define TEGRA_CSI_INPUT_STREAM_CONTROL                  0x000
#define   CSI_SKIP_PACKET_THRESHOLD_OFFSET		16
#define TEGRA_CSI_PIXEL_STREAM_CONTROL0			0x004
#define   CSI_PP_PACKET_HEADER_SENT			BIT(4)
#define   CSI_PP_DATA_IDENTIFIER_ENABLE			BIT(5)
#define   CSI_PP_WORD_COUNT_SELECT_HEADER		BIT(6)
#define   CSI_PP_CRC_CHECK_ENABLE			BIT(7)
#define   CSI_PP_WC_CHECK				BIT(8)
#define   CSI_PP_OUTPUT_FORMAT_STORE			(0x3 << 16)
#define   CSI_PPA_PAD_LINE_NOPAD			(0x2 << 24)
#define   CSI_PP_HEADER_EC_DISABLE			(0x1 << 27)
#define   CSI_PPA_PAD_FRAME_NOPAD			(0x2 << 28)
#define TEGRA_CSI_PIXEL_STREAM_CONTROL1                 0x008
#define   CSI_PP_TOP_FIELD_FRAME_OFFSET			0
#define   CSI_PP_TOP_FIELD_FRAME_MASK_OFFSET		4
#define TEGRA_CSI_PIXEL_STREAM_GAP                      0x00c
#define   PP_FRAME_MIN_GAP_OFFSET			16
#define TEGRA_CSI_PIXEL_STREAM_PP_COMMAND               0x010
#define   CSI_PP_ENABLE					0x1
#define   CSI_PP_DISABLE				0x2
#define   CSI_PP_RST					0x3
#define   CSI_PP_SINGLE_SHOT_ENABLE			(0x1 << 2)
#define   CSI_PP_START_MARKER_FRAME_MAX_OFFSET		12
#define TEGRA_CSI_PIXEL_STREAM_EXPECTED_FRAME           0x014
#define TEGRA_CSI_PIXEL_PARSER_INTERRUPT_MASK           0x018
#define TEGRA_CSI_PIXEL_PARSER_STATUS                   0x01c

/* Tegra210 CSI PHY registers */
/* CSI_PHY_CIL_COMMAND_0 offset 0x0d0 from TEGRA_CSI_PIXEL_PARSER_0_BASE */
#define TEGRA_CSI_PHY_CIL_COMMAND                       0x0d0
#define   CSI_A_PHY_CIL_NOP				0x0
#define   CSI_A_PHY_CIL_ENABLE				0x1
#define   CSI_A_PHY_CIL_DISABLE				0x2
#define   CSI_A_PHY_CIL_ENABLE_MASK			0x3
#define   CSI_B_PHY_CIL_NOP				(0x0 << 8)
#define   CSI_B_PHY_CIL_ENABLE				(0x1 << 8)
#define   CSI_B_PHY_CIL_DISABLE				(0x2 << 8)
#define   CSI_B_PHY_CIL_ENABLE_MASK			(0x3 << 8)

#define TEGRA_CSI_CIL_PAD_CONFIG0                       0x000
#define   BRICK_CLOCK_A_4X				(0x1 << 16)
#define   BRICK_CLOCK_B_4X				(0x2 << 16)
#define TEGRA_CSI_CIL_PAD_CONFIG1                       0x004
#define TEGRA_CSI_CIL_PHY_CONTROL                       0x008
#define TEGRA_CSI_CIL_INTERRUPT_MASK                    0x00c
#define TEGRA_CSI_CIL_STATUS                            0x010
#define TEGRA_CSI_CILX_STATUS                           0x014
#define TEGRA_CSI_CIL_SW_SENSOR_RESET                   0x020

#define TEGRA_CSI_PATTERN_GENERATOR_CTRL		0x000
#define   PG_MODE_OFFSET				2
#define   PG_ENABLE					0x1
#define   PG_DISABLE					0x0
#define TEGRA_CSI_PG_BLANK				0x004
#define   PG_VBLANK_OFFSET				16
#define TEGRA_CSI_PG_PHASE				0x008
#define TEGRA_CSI_PG_RED_FREQ				0x00c
#define   PG_RED_VERT_INIT_FREQ_OFFSET			16
#define   PG_RED_HOR_INIT_FREQ_OFFSET			0
#define TEGRA_CSI_PG_RED_FREQ_RATE			0x010
#define TEGRA_CSI_PG_GREEN_FREQ				0x014
#define   PG_GREEN_VERT_INIT_FREQ_OFFSET		16
#define   PG_GREEN_HOR_INIT_FREQ_OFFSET			0
#define TEGRA_CSI_PG_GREEN_FREQ_RATE			0x018
#define TEGRA_CSI_PG_BLUE_FREQ				0x01c
#define   PG_BLUE_VERT_INIT_FREQ_OFFSET			16
#define   PG_BLUE_HOR_INIT_FREQ_OFFSET			0
#define TEGRA_CSI_PG_BLUE_FREQ_RATE			0x020
#define TEGRA_CSI_PG_AOHDR				0x024
#define TEGRA_CSI_CSI_SW_STATUS_RESET			0x214
#define TEGRA_CSI_CLKEN_OVERRIDE			0x218

#define TEGRA210_CSI_PORT_OFFSET			0x34
#define TEGRA210_CSI_CIL_OFFSET				0x0f4
#define TEGRA210_CSI_TPG_OFFSET				0x18c

#define CSI_PP_OFFSET(block)				((block) * 0x800)
#define TEGRA210_VI_CSI_BASE(x)				(0x100 + (x) * 0x100)

/* Tegra210 VI registers accessors */
static void tegra_vi_write(struct tegra_vi_channel *chan, unsigned int addr,
			   u32 val)
{
	writel_relaxed(val, chan->vi->iomem + addr);
}

static u32 tegra_vi_read(struct tegra_vi_channel *chan, unsigned int addr)
{
	return readl_relaxed(chan->vi->iomem + addr);
}

/* Tegra210 VI_CSI registers accessors */
static void vi_csi_write(struct tegra_vi_channel *chan, unsigned int addr,
			 u32 val)
{
	void __iomem *vi_csi_base;

	vi_csi_base = chan->vi->iomem + TEGRA210_VI_CSI_BASE(chan->portno);

	writel_relaxed(val, vi_csi_base + addr);
}

static u32 vi_csi_read(struct tegra_vi_channel *chan, unsigned int addr)
{
	void __iomem *vi_csi_base;

	vi_csi_base = chan->vi->iomem + TEGRA210_VI_CSI_BASE(chan->portno);

	return readl_relaxed(vi_csi_base + addr);
}

/*
 * Tegra210 VI channel capture operations
 */
static int tegra_channel_capture_setup(struct tegra_vi_channel *chan)
{
	u32 height = chan->format.height;
	u32 width = chan->format.width;
	u32 format = chan->fmtinfo->img_fmt;
	u32 data_type = chan->fmtinfo->img_dt;
	u32 word_count = (width * chan->fmtinfo->bit_width) / 8;

	vi_csi_write(chan, TEGRA_VI_CSI_ERROR_STATUS, 0xffffffff);
	vi_csi_write(chan, TEGRA_VI_CSI_IMAGE_DEF,
		     ((chan->pg_mode ? 0 : 1) << BYPASS_PXL_TRANSFORM_OFFSET) |
		     (format << IMAGE_DEF_FORMAT_OFFSET) |
		     IMAGE_DEF_DEST_MEM);
	vi_csi_write(chan, TEGRA_VI_CSI_IMAGE_DT, data_type);
	vi_csi_write(chan, TEGRA_VI_CSI_IMAGE_SIZE_WC, word_count);
	vi_csi_write(chan, TEGRA_VI_CSI_IMAGE_SIZE,
		     (height << IMAGE_SIZE_HEIGHT_OFFSET) | width);
	return 0;
}

static void tegra_channel_vi_soft_reset(struct tegra_vi_channel *chan)
{
	/* disable clock gating to enable continuous clock */
	tegra_vi_write(chan, TEGRA_VI_CFG_CG_CTRL, 0);
	/*
	 * Soft reset memory client interface, pixel format logic, sensor
	 * control logic, and a shadow copy logic to bring VI to clean state.
	 */
	vi_csi_write(chan, TEGRA_VI_CSI_SW_RESET, 0xf);
	usleep_range(100, 200);
	vi_csi_write(chan, TEGRA_VI_CSI_SW_RESET, 0x0);

	/* enable back VI clock gating */
	tegra_vi_write(chan, TEGRA_VI_CFG_CG_CTRL, VI_CG_2ND_LEVEL_EN);
}

static void tegra_channel_capture_error_recover(struct tegra_vi_channel *chan)
{
	struct v4l2_subdev *subdev;
	u32 val;

	/*
	 * Recover VI and CSI hardware blocks in case of missing frame start
	 * events due to source not streaming or noisy csi inputs from the
	 * external source or many outstanding frame start or MW_ACK_DONE
	 * events which can cause CSI and VI hardware hang.
	 * This helps to have a clean capture for next frame.
	 */
	val = vi_csi_read(chan, TEGRA_VI_CSI_ERROR_STATUS);
	dev_dbg(&chan->video.dev, "TEGRA_VI_CSI_ERROR_STATUS 0x%08x\n", val);
	vi_csi_write(chan, TEGRA_VI_CSI_ERROR_STATUS, val);

	val = tegra_vi_read(chan, TEGRA_VI_CFG_VI_INCR_SYNCPT_ERROR);
	dev_dbg(&chan->video.dev,
		"TEGRA_VI_CFG_VI_INCR_SYNCPT_ERROR 0x%08x\n", val);
	tegra_vi_write(chan, TEGRA_VI_CFG_VI_INCR_SYNCPT_ERROR, val);

	/* recover VI by issuing software reset and re-setup for capture */
	tegra_channel_vi_soft_reset(chan);
	tegra_channel_capture_setup(chan);

	/* recover CSI block */
	subdev = tegra_channel_get_remote_subdev(chan);
	tegra_csi_error_recover(subdev);
}

static struct tegra_channel_buffer *
dequeue_buf_done(struct tegra_vi_channel *chan)
{
	struct tegra_channel_buffer *buf = NULL;

	spin_lock(&chan->done_lock);
	if (list_empty(&chan->done)) {
		spin_unlock(&chan->done_lock);
		return NULL;
	}

	buf = list_first_entry(&chan->done,
			       struct tegra_channel_buffer, queue);
	if (buf)
		list_del_init(&buf->queue);
	spin_unlock(&chan->done_lock);

	return buf;
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

static int tegra_channel_capture_frame(struct tegra_vi_channel *chan,
				       struct tegra_channel_buffer *buf)
{
	u32 thresh, value, frame_start, mw_ack_done;
	int bytes_per_line = chan->format.bytesperline;
	int err;

	/* program buffer address by using surface 0 */
	vi_csi_write(chan, TEGRA_VI_CSI_SURFACE0_OFFSET_MSB,
		     (u64)buf->addr >> 32);
	vi_csi_write(chan, TEGRA_VI_CSI_SURFACE0_OFFSET_LSB, buf->addr);
	vi_csi_write(chan, TEGRA_VI_CSI_SURFACE0_STRIDE, bytes_per_line);

	/*
	 * Tegra VI block interacts with host1x syncpt for synchronizing
	 * programmed condition of capture state and hardware operation.
	 * Frame start and Memory write acknowledge syncpts has their own
	 * FIFO of depth 2.
	 *
	 * Syncpoint trigger conditions set through VI_INCR_SYNCPT register
	 * are added to HW syncpt FIFO and when the HW triggers, syncpt
	 * condition is removed from the FIFO and counter at syncpoint index
	 * will be incremented by the hardware and software can wait for
	 * counter to reach threshold to synchronize capturing frame with the
	 * hardware capture events.
	 */

	/* increase channel syncpoint threshold for FRAME_START */
	thresh = host1x_syncpt_incr_max(chan->frame_start_sp, 1);

	/* Program FRAME_START trigger condition syncpt request */
	frame_start = VI_CSI_PP_FRAME_START(chan->portno);
	value = VI_CFG_VI_INCR_SYNCPT_COND(frame_start) |
		host1x_syncpt_id(chan->frame_start_sp);
	tegra_vi_write(chan, TEGRA_VI_CFG_VI_INCR_SYNCPT, value);

	/* increase channel syncpoint threshold for MW_ACK_DONE */
	buf->mw_ack_sp_thresh = host1x_syncpt_incr_max(chan->mw_ack_sp, 1);

	/* Program MW_ACK_DONE trigger condition syncpt request */
	mw_ack_done = VI_CSI_MW_ACK_DONE(chan->portno);
	value = VI_CFG_VI_INCR_SYNCPT_COND(mw_ack_done) |
		host1x_syncpt_id(chan->mw_ack_sp);
	tegra_vi_write(chan, TEGRA_VI_CFG_VI_INCR_SYNCPT, value);

	/* enable single shot capture */
	vi_csi_write(chan, TEGRA_VI_CSI_SINGLE_SHOT, SINGLE_SHOT_CAPTURE);

	/* wait for syncpt counter to reach frame start event threshold */
	err = host1x_syncpt_wait(chan->frame_start_sp, thresh,
				 TEGRA_VI_SYNCPT_WAIT_TIMEOUT, &value);
	if (err) {
		dev_err_ratelimited(&chan->video.dev,
				    "frame start syncpt timeout: %d\n", err);
		/* increment syncpoint counter for timedout events */
		host1x_syncpt_incr(chan->frame_start_sp);
		spin_lock(&chan->sp_incr_lock);
		host1x_syncpt_incr(chan->mw_ack_sp);
		spin_unlock(&chan->sp_incr_lock);
		/* clear errors and recover */
		tegra_channel_capture_error_recover(chan);
		release_buffer(chan, buf, VB2_BUF_STATE_ERROR);
		return err;
	}

	/* move buffer to capture done queue */
	spin_lock(&chan->done_lock);
	list_add_tail(&buf->queue, &chan->done);
	spin_unlock(&chan->done_lock);

	/* wait up kthread for capture done */
	wake_up_interruptible(&chan->done_wait);

	return 0;
}

static void tegra_channel_capture_done(struct tegra_vi_channel *chan,
				       struct tegra_channel_buffer *buf)
{
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	u32 value;
	int ret;

	/* wait for syncpt counter to reach MW_ACK_DONE event threshold */
	ret = host1x_syncpt_wait(chan->mw_ack_sp, buf->mw_ack_sp_thresh,
				 TEGRA_VI_SYNCPT_WAIT_TIMEOUT, &value);
	if (ret) {
		dev_err_ratelimited(&chan->video.dev,
				    "MW_ACK_DONE syncpt timeout: %d\n", ret);
		state = VB2_BUF_STATE_ERROR;
		/* increment syncpoint counter for timedout event */
		spin_lock(&chan->sp_incr_lock);
		host1x_syncpt_incr(chan->mw_ack_sp);
		spin_unlock(&chan->sp_incr_lock);
	}

	release_buffer(chan, buf, state);
}

static int chan_capture_kthread_start(void *data)
{
	struct tegra_vi_channel *chan = data;
	struct tegra_channel_buffer *buf;
	int err = 0;

	while (1) {
		/*
		 * Source is not streaming if error is non-zero.
		 * So, do not dequeue buffers on error and let the thread sleep
		 * till kthread stop signal is received.
		 */
		wait_event_interruptible(chan->start_wait,
					 kthread_should_stop() ||
					 (!list_empty(&chan->capture) &&
					 !err));

		if (kthread_should_stop())
			break;

		/* dequeue the buffer and start capture */
		spin_lock(&chan->start_lock);
		if (list_empty(&chan->capture)) {
			spin_unlock(&chan->start_lock);
			continue;
		}

		buf = list_first_entry(&chan->capture,
				       struct tegra_channel_buffer, queue);
		list_del_init(&buf->queue);
		spin_unlock(&chan->start_lock);

		err = tegra_channel_capture_frame(chan, buf);
		if (err)
			vb2_queue_error(&chan->queue);
	}

	return 0;
}

static int chan_capture_kthread_finish(void *data)
{
	struct tegra_vi_channel *chan = data;
	struct tegra_channel_buffer *buf;

	while (1) {
		wait_event_interruptible(chan->done_wait,
					 !list_empty(&chan->done) ||
					 kthread_should_stop());

		/* dequeue buffers and finish capture */
		buf = dequeue_buf_done(chan);
		while (buf) {
			tegra_channel_capture_done(chan, buf);
			buf = dequeue_buf_done(chan);
		}

		if (kthread_should_stop())
			break;
	}

	return 0;
}

static int tegra210_vi_start_streaming(struct vb2_queue *vq, u32 count)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);
	struct media_pipeline *pipe = &chan->video.pipe;
	u32 val;
	int ret;

	tegra_vi_write(chan, TEGRA_VI_CFG_CG_CTRL, VI_CG_2ND_LEVEL_EN);

	/* clear errors */
	val = vi_csi_read(chan, TEGRA_VI_CSI_ERROR_STATUS);
	vi_csi_write(chan, TEGRA_VI_CSI_ERROR_STATUS, val);

	val = tegra_vi_read(chan, TEGRA_VI_CFG_VI_INCR_SYNCPT_ERROR);
	tegra_vi_write(chan, TEGRA_VI_CFG_VI_INCR_SYNCPT_ERROR, val);

	/*
	 * Sync point FIFO full stalls the host interface.
	 * Setting NO_STALL will drop INCR_SYNCPT methods when fifos are
	 * full and the corresponding condition bits in INCR_SYNCPT_ERROR
	 * register will be set.
	 * This allows SW to process error recovery.
	 */
	tegra_vi_write(chan, TEGRA_VI_CFG_VI_INCR_SYNCPT_CNTRL,
		       VI_INCR_SYNCPT_NO_STALL);

	/* start the pipeline */
	ret = media_pipeline_start(&chan->video.entity, pipe);
	if (ret < 0)
		goto error_pipeline_start;

	tegra_channel_capture_setup(chan);
	ret = tegra_channel_set_stream(chan, true);
	if (ret < 0)
		goto error_set_stream;

	chan->sequence = 0;

	/* start kthreads to capture data to buffer and return them */
	chan->kthread_start_capture = kthread_run(chan_capture_kthread_start,
						  chan, "%s:0",
						  chan->video.name);
	if (IS_ERR(chan->kthread_start_capture)) {
		ret = PTR_ERR(chan->kthread_start_capture);
		chan->kthread_start_capture = NULL;
		dev_err(&chan->video.dev,
			"failed to run capture start kthread: %d\n", ret);
		goto error_kthread_start;
	}

	chan->kthread_finish_capture = kthread_run(chan_capture_kthread_finish,
						   chan, "%s:1",
						   chan->video.name);
	if (IS_ERR(chan->kthread_finish_capture)) {
		ret = PTR_ERR(chan->kthread_finish_capture);
		chan->kthread_finish_capture = NULL;
		dev_err(&chan->video.dev,
			"failed to run capture finish kthread: %d\n", ret);
		goto error_kthread_done;
	}

	return 0;

error_kthread_done:
	kthread_stop(chan->kthread_start_capture);
error_kthread_start:
	tegra_channel_set_stream(chan, false);
error_set_stream:
	media_pipeline_stop(&chan->video.entity);
error_pipeline_start:
	tegra_channel_release_buffers(chan, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void tegra210_vi_stop_streaming(struct vb2_queue *vq)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);

	if (chan->kthread_start_capture) {
		kthread_stop(chan->kthread_start_capture);
		chan->kthread_start_capture = NULL;
	}

	if (chan->kthread_finish_capture) {
		kthread_stop(chan->kthread_finish_capture);
		chan->kthread_finish_capture = NULL;
	}

	tegra_channel_release_buffers(chan, VB2_BUF_STATE_ERROR);
	tegra_channel_set_stream(chan, false);
	media_pipeline_stop(&chan->video.entity);
}

/*
 * Tegra210 VI Pixel memory format enum.
 * These format enum value gets programmed into corresponding Tegra VI
 * channel register bits.
 */
enum tegra210_image_format {
	TEGRA210_IMAGE_FORMAT_T_L8 = 16,

	TEGRA210_IMAGE_FORMAT_T_R16_I = 32,
	TEGRA210_IMAGE_FORMAT_T_B5G6R5,
	TEGRA210_IMAGE_FORMAT_T_R5G6B5,
	TEGRA210_IMAGE_FORMAT_T_A1B5G5R5,
	TEGRA210_IMAGE_FORMAT_T_A1R5G5B5,
	TEGRA210_IMAGE_FORMAT_T_B5G5R5A1,
	TEGRA210_IMAGE_FORMAT_T_R5G5B5A1,
	TEGRA210_IMAGE_FORMAT_T_A4B4G4R4,
	TEGRA210_IMAGE_FORMAT_T_A4R4G4B4,
	TEGRA210_IMAGE_FORMAT_T_B4G4R4A4,
	TEGRA210_IMAGE_FORMAT_T_R4G4B4A4,

	TEGRA210_IMAGE_FORMAT_T_A8B8G8R8 = 64,
	TEGRA210_IMAGE_FORMAT_T_A8R8G8B8,
	TEGRA210_IMAGE_FORMAT_T_B8G8R8A8,
	TEGRA210_IMAGE_FORMAT_T_R8G8B8A8,
	TEGRA210_IMAGE_FORMAT_T_A2B10G10R10,
	TEGRA210_IMAGE_FORMAT_T_A2R10G10B10,
	TEGRA210_IMAGE_FORMAT_T_B10G10R10A2,
	TEGRA210_IMAGE_FORMAT_T_R10G10B10A2,

	TEGRA210_IMAGE_FORMAT_T_A8Y8U8V8 = 193,
	TEGRA210_IMAGE_FORMAT_T_V8U8Y8A8,

	TEGRA210_IMAGE_FORMAT_T_A2Y10U10V10 = 197,
	TEGRA210_IMAGE_FORMAT_T_V10U10Y10A2,
	TEGRA210_IMAGE_FORMAT_T_Y8_U8__Y8_V8,
	TEGRA210_IMAGE_FORMAT_T_Y8_V8__Y8_U8,
	TEGRA210_IMAGE_FORMAT_T_U8_Y8__V8_Y8,
	TEGRA210_IMAGE_FORMAT_T_V8_Y8__U8_Y8,

	TEGRA210_IMAGE_FORMAT_T_Y8__U8__V8_N444 = 224,
	TEGRA210_IMAGE_FORMAT_T_Y8__U8V8_N444,
	TEGRA210_IMAGE_FORMAT_T_Y8__V8U8_N444,
	TEGRA210_IMAGE_FORMAT_T_Y8__U8__V8_N422,
	TEGRA210_IMAGE_FORMAT_T_Y8__U8V8_N422,
	TEGRA210_IMAGE_FORMAT_T_Y8__V8U8_N422,
	TEGRA210_IMAGE_FORMAT_T_Y8__U8__V8_N420,
	TEGRA210_IMAGE_FORMAT_T_Y8__U8V8_N420,
	TEGRA210_IMAGE_FORMAT_T_Y8__V8U8_N420,
	TEGRA210_IMAGE_FORMAT_T_X2LC10LB10LA10,
	TEGRA210_IMAGE_FORMAT_T_A2R6R6R6R6R6,
};

#define TEGRA210_VIDEO_FMT(DATA_TYPE, BIT_WIDTH, MBUS_CODE, BPP,	\
			   FORMAT, FOURCC)				\
{									\
	TEGRA_IMAGE_DT_##DATA_TYPE,					\
	BIT_WIDTH,							\
	MEDIA_BUS_FMT_##MBUS_CODE,					\
	BPP,								\
	TEGRA210_IMAGE_FORMAT_##FORMAT,					\
	V4L2_PIX_FMT_##FOURCC,						\
}

/* Tegra210 supported video formats */
static const struct tegra_video_format tegra210_video_formats[] = {
	/* RAW 8 */
	TEGRA210_VIDEO_FMT(RAW8, 8, SRGGB8_1X8, 1, T_L8, SRGGB8),
	TEGRA210_VIDEO_FMT(RAW8, 8, SGRBG8_1X8, 1, T_L8, SGRBG8),
	TEGRA210_VIDEO_FMT(RAW8, 8, SGBRG8_1X8, 1, T_L8, SGBRG8),
	TEGRA210_VIDEO_FMT(RAW8, 8, SBGGR8_1X8, 1, T_L8, SBGGR8),
	/* RAW 10 */
	TEGRA210_VIDEO_FMT(RAW10, 10, SRGGB10_1X10, 2, T_R16_I, SRGGB10),
	TEGRA210_VIDEO_FMT(RAW10, 10, SGRBG10_1X10, 2, T_R16_I, SGRBG10),
	TEGRA210_VIDEO_FMT(RAW10, 10, SGBRG10_1X10, 2, T_R16_I, SGBRG10),
	TEGRA210_VIDEO_FMT(RAW10, 10, SBGGR10_1X10, 2, T_R16_I, SBGGR10),
	/* RAW 12 */
	TEGRA210_VIDEO_FMT(RAW12, 12, SRGGB12_1X12, 2, T_R16_I, SRGGB12),
	TEGRA210_VIDEO_FMT(RAW12, 12, SGRBG12_1X12, 2, T_R16_I, SGRBG12),
	TEGRA210_VIDEO_FMT(RAW12, 12, SGBRG12_1X12, 2, T_R16_I, SGBRG12),
	TEGRA210_VIDEO_FMT(RAW12, 12, SBGGR12_1X12, 2, T_R16_I, SBGGR12),
	/* RGB888 */
	TEGRA210_VIDEO_FMT(RGB888, 24, RGB888_1X24, 4, T_A8R8G8B8, RGB24),
	TEGRA210_VIDEO_FMT(RGB888, 24, RGB888_1X32_PADHI, 4, T_A8B8G8R8,
			   XBGR32),
	/* YUV422 */
	TEGRA210_VIDEO_FMT(YUV422_8, 16, UYVY8_1X16, 2, T_U8_Y8__V8_Y8, UYVY),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, VYUY8_1X16, 2, T_V8_Y8__U8_Y8, VYUY),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, YUYV8_1X16, 2, T_Y8_U8__Y8_V8, YUYV),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, YVYU8_1X16, 2, T_Y8_V8__Y8_U8, YVYU),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, UYVY8_1X16, 1, T_Y8__V8U8_N422, NV16),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, UYVY8_2X8, 2, T_U8_Y8__V8_Y8, UYVY),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, VYUY8_2X8, 2, T_V8_Y8__U8_Y8, VYUY),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, YUYV8_2X8, 2, T_Y8_U8__Y8_V8, YUYV),
	TEGRA210_VIDEO_FMT(YUV422_8, 16, YVYU8_2X8, 2, T_Y8_V8__Y8_U8, YVYU),
};

/* Tegra210 VI operations */
static const struct tegra_vi_ops tegra210_vi_ops = {
	.vi_start_streaming = tegra210_vi_start_streaming,
	.vi_stop_streaming = tegra210_vi_stop_streaming,
};

/* Tegra210 VI SoC data */
const struct tegra_vi_soc tegra210_vi_soc = {
	.video_formats = tegra210_video_formats,
	.nformats = ARRAY_SIZE(tegra210_video_formats),
	.ops = &tegra210_vi_ops,
	.hw_revision = 3,
	.vi_max_channels = 6,
	.vi_max_clk_hz = 499200000,
};

/* Tegra210 CSI PHY registers accessors */
static void csi_write(struct tegra_csi *csi, u8 portno, unsigned int addr,
		      u32 val)
{
	void __iomem *csi_pp_base;

	csi_pp_base = csi->iomem + CSI_PP_OFFSET(portno >> 1);

	writel_relaxed(val, csi_pp_base + addr);
}

/* Tegra210 CSI Pixel parser registers accessors */
static void pp_write(struct tegra_csi *csi, u8 portno, u32 addr, u32 val)
{
	void __iomem *csi_pp_base;
	unsigned int offset;

	csi_pp_base = csi->iomem + CSI_PP_OFFSET(portno >> 1);
	offset = (portno % CSI_PORTS_PER_BRICK) * TEGRA210_CSI_PORT_OFFSET;

	writel_relaxed(val, csi_pp_base + offset + addr);
}

static u32 pp_read(struct tegra_csi *csi, u8 portno, u32 addr)
{
	void __iomem *csi_pp_base;
	unsigned int offset;

	csi_pp_base = csi->iomem + CSI_PP_OFFSET(portno >> 1);
	offset = (portno % CSI_PORTS_PER_BRICK) * TEGRA210_CSI_PORT_OFFSET;

	return readl_relaxed(csi_pp_base + offset + addr);
}

/* Tegra210 CSI CIL A/B port registers accessors */
static void cil_write(struct tegra_csi *csi, u8 portno, u32 addr, u32 val)
{
	void __iomem *csi_cil_base;
	unsigned int offset;

	csi_cil_base = csi->iomem + CSI_PP_OFFSET(portno >> 1) +
		       TEGRA210_CSI_CIL_OFFSET;
	offset = (portno % CSI_PORTS_PER_BRICK) * TEGRA210_CSI_PORT_OFFSET;

	writel_relaxed(val, csi_cil_base + offset + addr);
}

static u32 cil_read(struct tegra_csi *csi, u8 portno, u32 addr)
{
	void __iomem *csi_cil_base;
	unsigned int offset;

	csi_cil_base = csi->iomem + CSI_PP_OFFSET(portno >> 1) +
		       TEGRA210_CSI_CIL_OFFSET;
	offset = (portno % CSI_PORTS_PER_BRICK) * TEGRA210_CSI_PORT_OFFSET;

	return readl_relaxed(csi_cil_base + offset + addr);
}

/* Tegra210 CSI Test pattern generator registers accessor */
static void tpg_write(struct tegra_csi *csi, u8 portno, unsigned int addr,
		      u32 val)
{
	void __iomem *csi_pp_base;
	unsigned int offset;

	csi_pp_base = csi->iomem + CSI_PP_OFFSET(portno >> 1);
	offset = (portno % CSI_PORTS_PER_BRICK) * TEGRA210_CSI_PORT_OFFSET +
		 TEGRA210_CSI_TPG_OFFSET;

	writel_relaxed(val, csi_pp_base + offset + addr);
}

/*
 * Tegra210 CSI operations
 */
static void tegra210_csi_error_recover(struct tegra_csi_channel *csi_chan)
{
	struct tegra_csi *csi = csi_chan->csi;
	unsigned int portno = csi_chan->csi_port_num;
	u32 val;

	/*
	 * Recover CSI hardware in case of capture errors by issuing
	 * software reset to CSICIL sensor, pixel parser, and clear errors
	 * to have clean capture on  next streaming.
	 */
	val = pp_read(csi, portno, TEGRA_CSI_PIXEL_PARSER_STATUS);
	dev_dbg(csi->dev, "TEGRA_CSI_PIXEL_PARSER_STATUS 0x%08x\n", val);

	val = cil_read(csi, portno, TEGRA_CSI_CIL_STATUS);
	dev_dbg(csi->dev, "TEGRA_CSI_CIL_STATUS 0x%08x\n", val);

	val = cil_read(csi, portno, TEGRA_CSI_CILX_STATUS);
	dev_dbg(csi->dev, "TEGRA_CSI_CILX_STATUS 0x%08x\n", val);

	if (csi_chan->numlanes == 4) {
		/* reset CSI CIL sensor */
		cil_write(csi, portno, TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x1);
		cil_write(csi, portno + 1, TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x1);
		/*
		 * SW_STATUS_RESET resets all status bits of PPA, PPB, CILA,
		 * CILB status registers and debug counters.
		 * So, SW_STATUS_RESET can be used only when CSI brick is in
		 * x4 mode.
		 */
		csi_write(csi, portno, TEGRA_CSI_CSI_SW_STATUS_RESET, 0x1);

		/* sleep for 20 clock cycles to drain the FIFO */
		usleep_range(10, 20);

		cil_write(csi, portno + 1, TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x0);
		cil_write(csi, portno, TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x0);
		csi_write(csi, portno, TEGRA_CSI_CSI_SW_STATUS_RESET, 0x0);
	} else {
		/* reset CSICIL sensor */
		cil_write(csi, portno, TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x1);
		usleep_range(10, 20);
		cil_write(csi, portno, TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x0);

		/* clear the errors */
		pp_write(csi, portno, TEGRA_CSI_PIXEL_PARSER_STATUS,
			 0xffffffff);
		cil_write(csi, portno, TEGRA_CSI_CIL_STATUS, 0xffffffff);
		cil_write(csi, portno, TEGRA_CSI_CILX_STATUS, 0xffffffff);
	}
}

static int tegra210_csi_start_streaming(struct tegra_csi_channel *csi_chan)
{
	struct tegra_csi *csi = csi_chan->csi;
	unsigned int portno = csi_chan->csi_port_num;
	u32 val;

	csi_write(csi, portno, TEGRA_CSI_CLKEN_OVERRIDE, 0);

	/* clean up status */
	pp_write(csi, portno, TEGRA_CSI_PIXEL_PARSER_STATUS, 0xffffffff);
	cil_write(csi, portno, TEGRA_CSI_CIL_STATUS, 0xffffffff);
	cil_write(csi, portno, TEGRA_CSI_CILX_STATUS, 0xffffffff);
	cil_write(csi, portno, TEGRA_CSI_CIL_INTERRUPT_MASK, 0x0);

	/* CIL PHY registers setup */
	cil_write(csi, portno, TEGRA_CSI_CIL_PAD_CONFIG0, 0x0);
	cil_write(csi, portno, TEGRA_CSI_CIL_PHY_CONTROL, 0xa);

	/*
	 * The CSI unit provides for connection of up to six cameras in
	 * the system and is organized as three identical instances of
	 * two MIPI support blocks, each with a separate 4-lane
	 * interface that can be configured as a single camera with 4
	 * lanes or as a dual camera with 2 lanes available for each
	 * camera.
	 */
	if (csi_chan->numlanes == 4) {
		cil_write(csi, portno + 1, TEGRA_CSI_CIL_STATUS, 0xffffffff);
		cil_write(csi, portno + 1, TEGRA_CSI_CILX_STATUS, 0xffffffff);
		cil_write(csi, portno + 1, TEGRA_CSI_CIL_INTERRUPT_MASK, 0x0);

		cil_write(csi, portno, TEGRA_CSI_CIL_PAD_CONFIG0,
			  BRICK_CLOCK_A_4X);
		cil_write(csi, portno + 1, TEGRA_CSI_CIL_PAD_CONFIG0, 0x0);
		cil_write(csi, portno + 1, TEGRA_CSI_CIL_INTERRUPT_MASK, 0x0);
		cil_write(csi, portno + 1, TEGRA_CSI_CIL_PHY_CONTROL, 0xa);
		csi_write(csi, portno, TEGRA_CSI_PHY_CIL_COMMAND,
			  CSI_A_PHY_CIL_ENABLE | CSI_B_PHY_CIL_ENABLE);
	} else {
		val = ((portno & 1) == PORT_A) ?
		      CSI_A_PHY_CIL_ENABLE | CSI_B_PHY_CIL_NOP :
		      CSI_B_PHY_CIL_ENABLE | CSI_A_PHY_CIL_NOP;
		csi_write(csi, portno, TEGRA_CSI_PHY_CIL_COMMAND, val);
	}

	/* CSI pixel parser registers setup */
	pp_write(csi, portno, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND,
		 (0xf << CSI_PP_START_MARKER_FRAME_MAX_OFFSET) |
		 CSI_PP_SINGLE_SHOT_ENABLE | CSI_PP_RST);
	pp_write(csi, portno, TEGRA_CSI_PIXEL_PARSER_INTERRUPT_MASK, 0x0);
	pp_write(csi, portno, TEGRA_CSI_PIXEL_STREAM_CONTROL0,
		 CSI_PP_PACKET_HEADER_SENT |
		 CSI_PP_DATA_IDENTIFIER_ENABLE |
		 CSI_PP_WORD_COUNT_SELECT_HEADER |
		 CSI_PP_CRC_CHECK_ENABLE |  CSI_PP_WC_CHECK |
		 CSI_PP_OUTPUT_FORMAT_STORE | CSI_PPA_PAD_LINE_NOPAD |
		 CSI_PP_HEADER_EC_DISABLE | CSI_PPA_PAD_FRAME_NOPAD |
		 (portno & 1));
	pp_write(csi, portno, TEGRA_CSI_PIXEL_STREAM_CONTROL1,
		 (0x1 << CSI_PP_TOP_FIELD_FRAME_OFFSET) |
		 (0x1 << CSI_PP_TOP_FIELD_FRAME_MASK_OFFSET));
	pp_write(csi, portno, TEGRA_CSI_PIXEL_STREAM_GAP,
		 0x14 << PP_FRAME_MIN_GAP_OFFSET);
	pp_write(csi, portno, TEGRA_CSI_PIXEL_STREAM_EXPECTED_FRAME, 0x0);
	pp_write(csi, portno, TEGRA_CSI_INPUT_STREAM_CONTROL,
		 (0x3f << CSI_SKIP_PACKET_THRESHOLD_OFFSET) |
		 (csi_chan->numlanes - 1));

	/* TPG setup */
	if (csi_chan->pg_mode) {
		tpg_write(csi, portno, TEGRA_CSI_PATTERN_GENERATOR_CTRL,
			  ((csi_chan->pg_mode - 1) << PG_MODE_OFFSET) |
			  PG_ENABLE);
		tpg_write(csi, portno, TEGRA_CSI_PG_BLANK,
			  csi_chan->v_blank << PG_VBLANK_OFFSET |
			  csi_chan->h_blank);
		tpg_write(csi, portno, TEGRA_CSI_PG_PHASE, 0x0);
		tpg_write(csi, portno, TEGRA_CSI_PG_RED_FREQ,
			  (0x10 << PG_RED_VERT_INIT_FREQ_OFFSET) |
			  (0x10 << PG_RED_HOR_INIT_FREQ_OFFSET));
		tpg_write(csi, portno, TEGRA_CSI_PG_RED_FREQ_RATE, 0x0);
		tpg_write(csi, portno, TEGRA_CSI_PG_GREEN_FREQ,
			  (0x10 << PG_GREEN_VERT_INIT_FREQ_OFFSET) |
			  (0x10 << PG_GREEN_HOR_INIT_FREQ_OFFSET));
		tpg_write(csi, portno, TEGRA_CSI_PG_GREEN_FREQ_RATE, 0x0);
		tpg_write(csi, portno, TEGRA_CSI_PG_BLUE_FREQ,
			  (0x10 << PG_BLUE_VERT_INIT_FREQ_OFFSET) |
			  (0x10 << PG_BLUE_HOR_INIT_FREQ_OFFSET));
		tpg_write(csi, portno, TEGRA_CSI_PG_BLUE_FREQ_RATE, 0x0);
	}

	pp_write(csi, portno, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND,
		 (0xf << CSI_PP_START_MARKER_FRAME_MAX_OFFSET) |
		 CSI_PP_SINGLE_SHOT_ENABLE | CSI_PP_ENABLE);

	return 0;
}

static void tegra210_csi_stop_streaming(struct tegra_csi_channel *csi_chan)
{
	struct tegra_csi *csi = csi_chan->csi;
	unsigned int portno = csi_chan->csi_port_num;
	u32 val;

	val = pp_read(csi, portno, TEGRA_CSI_PIXEL_PARSER_STATUS);

	dev_dbg(csi->dev, "TEGRA_CSI_PIXEL_PARSER_STATUS 0x%08x\n", val);
	pp_write(csi, portno, TEGRA_CSI_PIXEL_PARSER_STATUS, val);

	val = cil_read(csi, portno, TEGRA_CSI_CIL_STATUS);
	dev_dbg(csi->dev, "TEGRA_CSI_CIL_STATUS 0x%08x\n", val);
	cil_write(csi, portno, TEGRA_CSI_CIL_STATUS, val);

	val = cil_read(csi, portno, TEGRA_CSI_CILX_STATUS);
	dev_dbg(csi->dev, "TEGRA_CSI_CILX_STATUS 0x%08x\n", val);
	cil_write(csi, portno, TEGRA_CSI_CILX_STATUS, val);

	pp_write(csi, portno, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND,
		 (0xf << CSI_PP_START_MARKER_FRAME_MAX_OFFSET) |
		 CSI_PP_DISABLE);

	if (csi_chan->pg_mode) {
		tpg_write(csi, portno, TEGRA_CSI_PATTERN_GENERATOR_CTRL,
			  PG_DISABLE);
		return;
	}

	if (csi_chan->numlanes == 4) {
		csi_write(csi, portno, TEGRA_CSI_PHY_CIL_COMMAND,
			  CSI_A_PHY_CIL_DISABLE |
			  CSI_B_PHY_CIL_DISABLE);
	} else {
		val = ((portno & 1) == PORT_A) ?
		      CSI_A_PHY_CIL_DISABLE | CSI_B_PHY_CIL_NOP :
		      CSI_B_PHY_CIL_DISABLE | CSI_A_PHY_CIL_NOP;
		csi_write(csi, portno, TEGRA_CSI_PHY_CIL_COMMAND, val);
	}
}

/*
 * Tegra210 CSI TPG frame rate table with horizontal and vertical
 * blanking intervals for corresponding format and resolution.
 * Blanking intervals are tuned values from design team for max TPG
 * clock rate.
 */
static const struct tpg_framerate tegra210_tpg_frmrate_table[] = {
	{
		.frmsize = { 1280, 720 },
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.framerate = 120,
		.h_blank = 512,
		.v_blank = 8,
	},
	{
		.frmsize = { 1920, 1080 },
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.framerate = 60,
		.h_blank = 512,
		.v_blank = 8,
	},
	{
		.frmsize = { 3840, 2160 },
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.framerate = 20,
		.h_blank = 8,
		.v_blank = 8,
	},
	{
		.frmsize = { 1280, 720 },
		.code = MEDIA_BUS_FMT_RGB888_1X32_PADHI,
		.framerate = 60,
		.h_blank = 512,
		.v_blank = 8,
	},
	{
		.frmsize = { 1920, 1080 },
		.code = MEDIA_BUS_FMT_RGB888_1X32_PADHI,
		.framerate = 30,
		.h_blank = 512,
		.v_blank = 8,
	},
	{
		.frmsize = { 3840, 2160 },
		.code = MEDIA_BUS_FMT_RGB888_1X32_PADHI,
		.framerate = 8,
		.h_blank = 8,
		.v_blank = 8,
	},
};

static const char * const tegra210_csi_cil_clks[] = {
	"csi",
	"cilab",
	"cilcd",
	"cile",
	"csi_tpg",
};

/* Tegra210 CSI operations */
static const struct tegra_csi_ops tegra210_csi_ops = {
	.csi_start_streaming = tegra210_csi_start_streaming,
	.csi_stop_streaming = tegra210_csi_stop_streaming,
	.csi_err_recover = tegra210_csi_error_recover,
};

/* Tegra210 CSI SoC data */
const struct tegra_csi_soc tegra210_csi_soc = {
	.ops = &tegra210_csi_ops,
	.csi_max_channels = 6,
	.clk_names = tegra210_csi_cil_clks,
	.num_clks = ARRAY_SIZE(tegra210_csi_cil_clks),
	.tpg_frmrate_table = tegra210_tpg_frmrate_table,
	.tpg_frmrate_table_size = ARRAY_SIZE(tegra210_tpg_frmrate_table),
};
