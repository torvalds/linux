/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __TEGRA_VI_H__
#define __TEGRA_VI_H__

#include <linux/host1x.h>
#include <linux/list.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#include "csi.h"

#define V4L2_CID_TEGRA_SYNCPT_TIMEOUT_RETRY	(V4L2_CTRL_CLASS_CAMERA | 0x1001)

#define TEGRA_MIN_WIDTH		32U
#define TEGRA_MAX_WIDTH		32768U
#define TEGRA_MIN_HEIGHT	32U
#define TEGRA_MAX_HEIGHT	32768U

#define TEGRA_DEF_WIDTH		1920
#define TEGRA_DEF_HEIGHT	1080
#define TEGRA_IMAGE_FORMAT_DEF	32

#define MAX_FORMAT_NUM		64
#define SURFACE_ALIGN_BYTES	64

enum tegra_vi_pg_mode {
	TEGRA_VI_PG_DISABLED = 0,
	TEGRA_VI_PG_DIRECT,
	TEGRA_VI_PG_PATCH,
};

/**
 * struct tegra_vi_ops - Tegra VI operations
 * @vi_start_streaming: starts media pipeline, subdevice streaming, sets up
 *		VI for capture and runs capture start and capture finish
 *		kthreads for capturing frames to buffer and returns them back.
 * @vi_stop_streaming: stops media pipeline and subdevice streaming and returns
 *		back any queued buffers.
 */
struct tegra_vi_ops {
	int (*vi_start_streaming)(struct vb2_queue *vq, u32 count);
	void (*vi_stop_streaming)(struct vb2_queue *vq);
};

/**
 * struct tegra_vi_soc - NVIDIA Tegra Video Input SoC structure
 *
 * @video_formats: supported video formats
 * @nformats: total video formats
 * @ops: vi operations
 * @hw_revision: VI hw_revision
 * @vi_max_channels: supported max streaming channels
 * @vi_max_clk_hz: VI clock max frequency
 */
struct tegra_vi_soc {
	const struct tegra_video_format *video_formats;
	const unsigned int nformats;
	const struct tegra_vi_ops *ops;
	u32 hw_revision;
	unsigned int vi_max_channels;
	unsigned int vi_max_clk_hz;
};

/**
 * struct tegra_vi - NVIDIA Tegra Video Input device structure
 *
 * @dev: device struct
 * @client: host1x_client struct
 * @iomem: register base
 * @clk: main clock for VI block
 * @vdd: vdd regulator for VI hardware, normally it is avdd_dsi_csi
 * @soc: pointer to SoC data structure
 * @ops: vi operations
 * @vi_chans: list head for VI channels
 */
struct tegra_vi {
	struct device *dev;
	struct host1x_client client;
	void __iomem *iomem;
	struct clk *clk;
	struct regulator *vdd;
	const struct tegra_vi_soc *soc;
	const struct tegra_vi_ops *ops;
	struct list_head vi_chans;
};

/**
 * struct tegra_vi_graph_entity - Entity in the video graph
 *
 * @asd: subdev asynchronous registration information
 * @entity: media entity from the corresponding V4L2 subdev
 * @subdev: V4L2 subdev
 */
struct tegra_vi_graph_entity {
	struct v4l2_async_subdev asd;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
};

/**
 * struct tegra_vi_channel - Tegra video channel
 *
 * @list: list head for this entry
 * @video: V4L2 video device associated with the video channel
 * @video_lock: protects the @format and @queue fields
 * @pad: media pad for the video device entity
 *
 * @vi: Tegra video input device structure
 * @frame_start_sp: host1x syncpoint pointer to synchronize programmed capture
 *		start condition with hardware frame start events through host1x
 *		syncpoint counters.
 * @mw_ack_sp: host1x syncpoint pointer to synchronize programmed memory write
 *		ack trigger condition with hardware memory write done at end of
 *		frame through host1x syncpoint counters.
 * @sp_incr_lock: protects cpu syncpoint increment.
 *
 * @kthread_start_capture: kthread to start capture of single frame when
 *		vb buffer is available. This thread programs VI CSI hardware
 *		for single frame capture and waits for frame start event from
 *		the hardware. On receiving frame start event, it wakes up
 *		kthread_finish_capture thread to wait for finishing frame data
 *		write to the memory. In case of missing frame start event, this
 *		thread returns buffer back to vb with VB2_BUF_STATE_ERROR.
 * @start_wait: waitqueue for starting frame capture when buffer is available.
 * @kthread_finish_capture: kthread to finish the buffer capture and return to.
 *		This thread is woken up by kthread_start_capture on receiving
 *		frame start event from the hardware and this thread waits for
 *		MW_ACK_DONE event which indicates completion of writing frame
 *		data to the memory. On receiving MW_ACK_DONE event, buffer is
 *		returned back to vb with VB2_BUF_STATE_DONE and in case of
 *		missing MW_ACK_DONE event, buffer is returned back to vb with
 *		VB2_BUF_STATE_ERROR.
 * @done_wait: waitqueue for finishing capture data writes to memory.
 *
 * @format: active V4L2 pixel format
 * @fmtinfo: format information corresponding to the active @format
 * @queue: vb2 buffers queue
 * @sequence: V4L2 buffers sequence number
 *
 * @capture: list of queued buffers for capture
 * @start_lock: protects the capture queued list
 * @done: list of capture done queued buffers
 * @done_lock: protects the capture done queue list
 *
 * @portnos: VI channel port numbers
 * @totalports: total number of ports used for this channel
 * @numgangports: number of ports combined together as a gang for capture
 * @of_node: device node of VI channel
 *
 * @ctrl_handler: V4L2 control handler of this video channel
 * @syncpt_timeout_retry: syncpt timeout retry count for the capture
 * @fmts_bitmap: a bitmap for supported formats matching v4l2 subdev formats
 * @tpg_fmts_bitmap: a bitmap for supported TPG formats
 * @pg_mode: test pattern generator mode (disabled/direct/patch)
 * @notifier: V4L2 asynchronous subdevs notifier
 */
struct tegra_vi_channel {
	struct list_head list;
	struct video_device video;
	/* protects the @format and @queue fields */
	struct mutex video_lock;
	struct media_pad pad;

	struct tegra_vi *vi;
	struct host1x_syncpt *frame_start_sp[GANG_PORTS_MAX];
	struct host1x_syncpt *mw_ack_sp[GANG_PORTS_MAX];
	/* protects the cpu syncpoint increment */
	spinlock_t sp_incr_lock[GANG_PORTS_MAX];

	struct task_struct *kthread_start_capture;
	wait_queue_head_t start_wait;
	struct task_struct *kthread_finish_capture;
	wait_queue_head_t done_wait;

	struct v4l2_pix_format format;
	const struct tegra_video_format *fmtinfo;
	struct vb2_queue queue;
	u32 sequence;

	struct list_head capture;
	/* protects the capture queued list */
	spinlock_t start_lock;
	struct list_head done;
	/* protects the capture done queue list */
	spinlock_t done_lock;

	unsigned char portnos[GANG_PORTS_MAX];
	u8 totalports;
	u8 numgangports;
	struct device_node *of_node;

	struct v4l2_ctrl_handler ctrl_handler;
	unsigned int syncpt_timeout_retry;
	DECLARE_BITMAP(fmts_bitmap, MAX_FORMAT_NUM);
	DECLARE_BITMAP(tpg_fmts_bitmap, MAX_FORMAT_NUM);
	enum tegra_vi_pg_mode pg_mode;

	struct v4l2_async_notifier notifier;
};

/**
 * struct tegra_channel_buffer - video channel buffer
 *
 * @buf: vb2 buffer base object
 * @queue: buffer list entry in the channel queued buffers list
 * @chan: channel that uses the buffer
 * @addr: Tegra IOVA buffer address for VI output
 * @mw_ack_sp_thresh: MW_ACK_DONE syncpoint threshold corresponding
 *		      to the capture buffer.
 */
struct tegra_channel_buffer {
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	struct tegra_vi_channel *chan;
	dma_addr_t addr;
	u32 mw_ack_sp_thresh[GANG_PORTS_MAX];
};

/*
 * VI channel input data type enum.
 * These data type enum value gets programmed into corresponding Tegra VI
 * channel register bits.
 */
enum tegra_image_dt {
	TEGRA_IMAGE_DT_YUV420_8 = 24,
	TEGRA_IMAGE_DT_YUV420_10,

	TEGRA_IMAGE_DT_YUV420CSPS_8 = 28,
	TEGRA_IMAGE_DT_YUV420CSPS_10,
	TEGRA_IMAGE_DT_YUV422_8,
	TEGRA_IMAGE_DT_YUV422_10,
	TEGRA_IMAGE_DT_RGB444,
	TEGRA_IMAGE_DT_RGB555,
	TEGRA_IMAGE_DT_RGB565,
	TEGRA_IMAGE_DT_RGB666,
	TEGRA_IMAGE_DT_RGB888,

	TEGRA_IMAGE_DT_RAW6 = 40,
	TEGRA_IMAGE_DT_RAW7,
	TEGRA_IMAGE_DT_RAW8,
	TEGRA_IMAGE_DT_RAW10,
	TEGRA_IMAGE_DT_RAW12,
	TEGRA_IMAGE_DT_RAW14,
};

/**
 * struct tegra_video_format - Tegra video format description
 *
 * @img_dt: image data type
 * @bit_width: format width in bits per component
 * @code: media bus format code
 * @bpp: bytes per pixel (when stored in memory)
 * @img_fmt: image format
 * @fourcc: V4L2 pixel format FCC identifier
 */
struct tegra_video_format {
	enum tegra_image_dt img_dt;
	unsigned int bit_width;
	unsigned int code;
	unsigned int bpp;
	u32 img_fmt;
	u32 fourcc;
};

#if defined(CONFIG_ARCH_TEGRA_210_SOC)
extern const struct tegra_vi_soc tegra210_vi_soc;
#endif

struct v4l2_subdev *
tegra_channel_get_remote_csi_subdev(struct tegra_vi_channel *chan);
struct v4l2_subdev *
tegra_channel_get_remote_source_subdev(struct tegra_vi_channel *chan);
int tegra_channel_set_stream(struct tegra_vi_channel *chan, bool on);
void tegra_channel_release_buffers(struct tegra_vi_channel *chan,
				   enum vb2_buffer_state state);
void tegra_channels_cleanup(struct tegra_vi *vi);
#endif
