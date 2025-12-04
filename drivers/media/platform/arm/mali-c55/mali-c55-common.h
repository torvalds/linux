/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Mali-C55 ISP Driver - Common definitions
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#ifndef _MALI_C55_COMMON_H
#define _MALI_C55_COMMON_H

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-isp.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

#define MALI_C55_DRIVER_NAME		"mali-c55"

/* min and max values for the image sizes */
#define MALI_C55_MIN_WIDTH		640U
#define MALI_C55_MIN_HEIGHT		480U
#define MALI_C55_MAX_WIDTH		8192U
#define MALI_C55_MAX_HEIGHT		8192U
#define MALI_C55_DEFAULT_WIDTH		1920U
#define MALI_C55_DEFAULT_HEIGHT		1080U

#define MALI_C55_DEFAULT_MEDIA_BUS_FMT	MEDIA_BUS_FMT_RGB121212_1X36

#define MALI_C55_NUM_CLKS		3
#define MALI_C55_NUM_RESETS		3

struct device;
struct mali_c55;
struct mali_c55_cap_dev;
struct media_pipeline;
struct mali_c55_params_buffer;
struct platform_device;
struct resource;

enum mali_c55_isp_pads {
	MALI_C55_ISP_PAD_SINK_VIDEO,
	MALI_C55_ISP_PAD_SOURCE_VIDEO,
	MALI_C55_ISP_PAD_SOURCE_BYPASS,
	MALI_C55_ISP_PAD_SOURCE_STATS,
	MALI_C55_ISP_PAD_SINK_PARAMS,
	MALI_C55_ISP_NUM_PADS,
};

struct mali_c55_tpg {
	struct mali_c55 *mali_c55;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct mali_c55_tpg_ctrls {
		struct v4l2_ctrl_handler handler;
		struct v4l2_ctrl *vblank;
	} ctrls;
};

struct mali_c55_isp {
	struct mali_c55 *mali_c55;
	struct v4l2_subdev sd;
	struct media_pad pads[MALI_C55_ISP_NUM_PADS];
	struct v4l2_ctrl_handler handler;
	struct media_pad *remote_src;
	/* Mutex to guard vb2 start/stop streaming */
	struct mutex capture_lock;
	unsigned int frame_sequence;
};

enum mali_c55_resizer_ids {
	MALI_C55_RSZ_FR,
	MALI_C55_RSZ_DS,
	MALI_C55_NUM_RSZS,
};

enum mali_c55_rsz_pads {
	MALI_C55_RSZ_SINK_PAD,
	MALI_C55_RSZ_SOURCE_PAD,
	MALI_C55_RSZ_SINK_BYPASS_PAD,
	MALI_C55_RSZ_NUM_PADS
};

struct mali_c55_resizer {
	struct mali_c55 *mali_c55;
	struct mali_c55_cap_dev *cap_dev;
	enum mali_c55_resizer_ids id;
	struct v4l2_subdev sd;
	struct media_pad pads[MALI_C55_RSZ_NUM_PADS];
	unsigned int num_routes;
};

enum mali_c55_cap_devs {
	MALI_C55_CAP_DEV_FR,
	MALI_C55_CAP_DEV_DS,
	MALI_C55_NUM_CAP_DEVS
};

struct mali_c55_format_info {
	u32 fourcc;
	/*
	 * The output formats can be produced by a couple of different media bus
	 * formats, depending on how the ISP is configured.
	 */
	unsigned int mbus_codes[2];
	bool is_raw;
	struct {
		u32 base_mode;
		u32 uv_plane;
	} registers;
};

struct mali_c55_isp_format_info {
	u32 code;
	u32 shifted_code;
	bool bypass;
	u32 order;
};

enum mali_c55_planes {
	MALI_C55_PLANE_Y,
	MALI_C55_PLANE_UV,
	MALI_C55_NUM_PLANES
};

struct mali_c55_buffer {
	struct vb2_v4l2_buffer vb;
	unsigned int planes_pending;
	struct list_head queue;
	dma_addr_t addrs[MALI_C55_NUM_PLANES];
};

struct mali_c55_cap_dev {
	struct mali_c55 *mali_c55;
	struct mali_c55_resizer *rsz;
	struct video_device vdev;
	struct media_pad pad;
	struct vb2_queue queue;
	/* Mutex to provide to vb2 */
	struct mutex lock;
	unsigned int reg_offset;

	struct {
		const struct mali_c55_format_info *info;
		struct v4l2_pix_format_mplane format;
	} format;

	struct {
		/* Spinlock to guard buffer queue */
		spinlock_t lock;
		/* Spinlock to guard the queue of buffers being processed */
		spinlock_t processing_lock;
		struct list_head input;
		struct list_head processing;
	} buffers;
};

struct mali_c55_stats_buf {
	struct vb2_v4l2_buffer vb;
	unsigned int segments_remaining;
	struct list_head queue;
	bool failed;
};

struct mali_c55_params_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	struct v4l2_isp_params_buffer *config;
};

struct mali_c55_stats {
	struct mali_c55 *mali_c55;
	struct video_device vdev;
	struct vb2_queue queue;
	struct media_pad pad;
	/* Mutex to provide to vb2 */
	struct mutex lock;

	struct {
		/* Spinlock to guard buffer queue */
		spinlock_t lock;
		struct list_head queue;
	} buffers;
};

struct mali_c55_params {
	struct mali_c55 *mali_c55;
	struct video_device vdev;
	struct vb2_queue queue;
	struct media_pad pad;
	/* Mutex to provide to vb2 */
	struct mutex lock;

	struct {
		/* Spinlock to guard buffer queue */
		spinlock_t lock;
		struct list_head queue;
	} buffers;
};

enum mali_c55_config_spaces {
	MALI_C55_CONFIG_PONG,
	MALI_C55_CONFIG_PING,
};

/**
 * struct mali_c55_context - Fields relating to a single camera context
 *
 * @mali_c55:	Pointer to the main struct mali_c55
 * @registers:	A pointer to some allocated memory holding register
 *		values to be written to the hardware at frame interrupt
 * @base:	Base address of the config space in the hardware
 * @lock:	A spinlock to protect against writes to @registers whilst that
 *		space is being copied to the hardware
 * @list:	A list head to facilitate a context queue
 */
struct mali_c55_context {
	struct mali_c55 *mali_c55;
	u32 *registers;
	phys_addr_t base;
	/* Spinlock to prevent simultaneous access of register space */
	spinlock_t lock;
	struct list_head list;
};

struct mali_c55 {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data clks[MALI_C55_NUM_CLKS];
	struct reset_control_bulk_data resets[MALI_C55_NUM_RESETS];
	int irqnum;

	u16 capabilities;
	bool inline_mode;
	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
	struct media_pipeline pipe;

	struct mali_c55_tpg tpg;
	struct mali_c55_isp isp;
	struct mali_c55_resizer resizers[MALI_C55_NUM_RSZS];
	struct mali_c55_cap_dev cap_devs[MALI_C55_NUM_CAP_DEVS];
	struct mali_c55_params params;
	struct mali_c55_stats stats;

	struct mali_c55_context context;
	u32 next_config;
};

void mali_c55_write(struct mali_c55 *mali_c55, unsigned int addr, u32 val);
void mali_c55_cap_dev_write(struct mali_c55_cap_dev *cap_dev, unsigned int addr,
			    u32 val);
void mali_c55_update_bits(struct mali_c55 *mali_c55, unsigned int addr,
			  u32 mask, u32 val);
u32 mali_c55_read(struct mali_c55 *mali_c55, unsigned int addr);
void mali_c55_ctx_write(struct mali_c55 *mali_c55, unsigned int addr, u32 val);
u32 mali_c55_ctx_read(struct mali_c55 *mali_c55, unsigned int addr);
void mali_c55_ctx_update_bits(struct mali_c55 *mali_c55, unsigned int addr,
			      u32 mask, u32 val);

int mali_c55_config_write(struct mali_c55_context *ctx,
			  enum mali_c55_config_spaces cfg_space,
			  bool force_synchronous);

int mali_c55_register_isp(struct mali_c55 *mali_c55);
int mali_c55_register_tpg(struct mali_c55 *mali_c55);
void mali_c55_unregister_tpg(struct mali_c55 *mali_c55);
void mali_c55_unregister_isp(struct mali_c55 *mali_c55);
int mali_c55_register_resizers(struct mali_c55 *mali_c55);
void mali_c55_unregister_resizers(struct mali_c55 *mali_c55);
int mali_c55_register_capture_devs(struct mali_c55 *mali_c55);
void mali_c55_unregister_capture_devs(struct mali_c55 *mali_c55);
int mali_c55_register_stats(struct mali_c55 *mali_c55);
void mali_c55_unregister_stats(struct mali_c55 *mali_c55);
int mali_c55_register_params(struct mali_c55 *mali_c55);
void mali_c55_unregister_params(struct mali_c55 *mali_c55);
struct mali_c55_context *mali_c55_get_active_context(struct mali_c55 *mali_c55);
void mali_c55_set_plane_done(struct mali_c55_cap_dev *cap_dev,
			     enum mali_c55_planes plane);
void mali_c55_set_next_buffer(struct mali_c55_cap_dev *cap_dev);
void mali_c55_isp_queue_event_sof(struct mali_c55 *mali_c55);

bool mali_c55_format_is_raw(unsigned int mbus_code);

const struct mali_c55_isp_format_info *
mali_c55_isp_fmt_next(const struct mali_c55_isp_format_info *fmt);
const struct mali_c55_isp_format_info *
mali_c55_isp_get_mbus_config_by_code(u32 code);
const struct mali_c55_isp_format_info *
mali_c55_isp_get_mbus_config_by_shifted_code(u32 code);
const struct mali_c55_isp_format_info *
mali_c55_isp_get_mbus_config_by_index(u32 index);
bool mali_c55_pipeline_ready(struct mali_c55 *mali_c55);
void mali_c55_stats_fill_buffer(struct mali_c55 *mali_c55,
				enum mali_c55_config_spaces cfg_space);
void mali_c55_params_write_config(struct mali_c55 *mali_c55);

#endif /* _MALI_C55_COMMON_H */
