/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 * Copyright (C) 2023 Mehdi Djait <mehdi.djait@bootlin.com>
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#ifndef _RKCIF_COMMON_H
#define _RKCIF_COMMON_H

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#include "rkcif-regs.h"

#define RKCIF_DRIVER_NAME "rockchip-cif"
#define RKCIF_CLK_MAX	  4

enum rkcif_format_type {
	RKCIF_FMT_TYPE_INVALID,
	RKCIF_FMT_TYPE_YUV,
	RKCIF_FMT_TYPE_RAW,
};

enum rkcif_id_index {
	RKCIF_ID0,
	RKCIF_ID1,
	RKCIF_ID2,
	RKCIF_ID3,
	RKCIF_ID_MAX
};

enum rkcif_interface_index {
	RKCIF_DVP,
	RKCIF_MIPI_BASE,
	RKCIF_MIPI1 = RKCIF_MIPI_BASE,
	RKCIF_MIPI2,
	RKCIF_MIPI3,
	RKCIF_MIPI4,
	RKCIF_MIPI5,
	RKCIF_MIPI6,
	RKCIF_MIPI_MAX,
	RKCIF_IF_MAX = RKCIF_MIPI_MAX
};

enum rkcif_interface_pad_index {
	RKCIF_IF_PAD_SINK,
	RKCIF_IF_PAD_SRC,
	RKCIF_IF_PAD_MAX
};

enum rkcif_interface_status {
	RKCIF_IF_INACTIVE,
	RKCIF_IF_ACTIVE,
};

enum rkcif_interface_type {
	RKCIF_IF_INVALID,
	RKCIF_IF_DVP,
	RKCIF_IF_MIPI,
};

enum rkcif_mipi_format_type {
	RKCIF_MIPI_TYPE_INVALID,
	RKCIF_MIPI_TYPE_RAW8,
	RKCIF_MIPI_TYPE_RAW10,
	RKCIF_MIPI_TYPE_RAW12,
	RKCIF_MIPI_TYPE_RGB888,
	RKCIF_MIPI_TYPE_YUV422SP,
	RKCIF_MIPI_TYPE_YUV420SP,
	RKCIF_MIPI_TYPE_YUV400,
};

struct rkcif_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	dma_addr_t buff_addr[VIDEO_MAX_PLANES];
	bool is_dummy;
};

struct rkcif_dummy_buffer {
	struct rkcif_buffer buffer;
	void *vaddr;
	u32 size;
};

enum rkcif_plane_index {
	RKCIF_PLANE_Y,
	RKCIF_PLANE_UV,
	RKCIF_PLANE_MAX
};

struct rkcif_input_fmt {
	u32 mbus_code;

	enum rkcif_format_type fmt_type;
	enum v4l2_field field;

	union {
		u32 dvp_fmt_val;
	};
};

struct rkcif_output_fmt {
	u32 fourcc;
	u32 mbus_code;
	u8 cplanes;
	u8 depth;

	union {
		u32 dvp_fmt_val;
		struct {
			u8 dt;
			bool compact;
			enum rkcif_mipi_format_type type;
		} mipi;
	};
};

struct rkcif_interface;

struct rkcif_remote {
	struct v4l2_async_connection async_conn;
	struct v4l2_subdev *sd;

	struct rkcif_interface *interface;
};

struct rkcif_stream {
	enum rkcif_id_index id;
	struct rkcif_device *rkcif;
	struct rkcif_interface *interface;
	const struct rkcif_output_fmt *out_fmts;
	unsigned int out_fmts_num;

	/* in ping-pong mode, two buffers can be provided to the HW */
	struct rkcif_buffer *buffers[2];
	int frame_idx;
	int frame_phase;

	/* in case of no available buffer, HW can write to the dummy buffer */
	struct rkcif_dummy_buffer dummy;

	bool stopping;
	wait_queue_head_t wq_stopped;

	/* queue of available buffers plus spinlock that protects it */
	spinlock_t driver_queue_lock;
	struct list_head driver_queue;

	/* lock used by the V4L2 core */
	struct mutex vlock;

	struct media_pad pad;
	struct media_pipeline pipeline;
	struct v4l2_pix_format_mplane pix;
	struct vb2_queue buf_queue;
	struct video_device vdev;

	void (*queue_buffer)(struct rkcif_stream *stream, unsigned int index);
	int (*start_streaming)(struct rkcif_stream *stream);
	void (*stop_streaming)(struct rkcif_stream *stream);
};

struct rkcif_dvp {
	u32 dvp_clk_delay;
};

struct rkcif_interface {
	enum rkcif_interface_type type;
	enum rkcif_interface_status status;
	enum rkcif_interface_index index;
	struct rkcif_device *rkcif;
	struct rkcif_remote *remote;
	struct rkcif_stream streams[RKCIF_ID_MAX];
	unsigned int streams_num;
	const struct rkcif_input_fmt *in_fmts;
	unsigned int in_fmts_num;

	struct media_pad pads[RKCIF_IF_PAD_MAX];
	struct v4l2_fwnode_endpoint vep;
	struct v4l2_subdev sd;

	union {
		struct rkcif_dvp dvp;
	};

	void (*set_crop)(struct rkcif_stream *stream, u16 left, u16 top);
};

struct rkcif_mipi_match_data {
	unsigned int mipi_num;
	unsigned int regs[RKCIF_MIPI_REGISTER_MAX];
	unsigned int regs_id[RKCIF_ID_MAX][RKCIF_MIPI_ID_REGISTER_MAX];
	u32 (*mipi_ctrl0)(struct rkcif_stream *stream,
			  const struct rkcif_output_fmt *active_out_fmt);
	struct {
		unsigned int offset;
	} blocks[RKCIF_MIPI_MAX - RKCIF_MIPI_BASE];
};

struct rkcif_dvp_match_data {
	const struct rkcif_input_fmt *in_fmts;
	unsigned int in_fmts_num;
	const struct rkcif_output_fmt *out_fmts;
	unsigned int out_fmts_num;
	void (*setup)(struct rkcif_device *rkcif);
	bool has_scaler;
	bool has_ids;
	unsigned int regs[RKCIF_DVP_REGISTER_MAX];
};

struct rkcif_match_data {
	const char *const *clks;
	unsigned int clks_num;
	const struct rkcif_dvp_match_data *dvp;
	const struct rkcif_mipi_match_data *mipi;
};

struct rkcif_device {
	struct device *dev;

	const struct rkcif_match_data *match_data;
	struct clk_bulk_data clks[RKCIF_CLK_MAX];
	unsigned int clks_num;
	struct regmap *grf;
	struct reset_control *reset;
	void __iomem *base_addr;

	struct rkcif_interface interfaces[RKCIF_IF_MAX];

	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
};

#endif
