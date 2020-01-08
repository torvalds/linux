/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Rockchip ISP1 Driver - Common definitions
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKISP1_COMMON_H
#define _RKISP1_COMMON_H

#include <linux/clk.h>
#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "rkisp1-regs.h"

#define RKISP1_ISP_MAX_WIDTH		4032
#define RKISP1_ISP_MAX_HEIGHT		3024
#define RKISP1_ISP_MIN_WIDTH		32
#define RKISP1_ISP_MIN_HEIGHT		32

#define RKISP1_RSZ_MP_SRC_MAX_WIDTH		4416
#define RKISP1_RSZ_MP_SRC_MAX_HEIGHT		3312
#define RKISP1_RSZ_SP_SRC_MAX_WIDTH		1920
#define RKISP1_RSZ_SP_SRC_MAX_HEIGHT		1920
#define RKISP1_RSZ_SRC_MIN_WIDTH		32
#define RKISP1_RSZ_SRC_MIN_HEIGHT		16

#define RKISP1_DEFAULT_WIDTH		800
#define RKISP1_DEFAULT_HEIGHT		600

#define RKISP1_DRIVER_NAME	"rkisp1"
#define RKISP1_BUS_INFO		"platform:" RKISP1_DRIVER_NAME

#define RKISP1_MAX_BUS_CLK	8

enum rkisp1_fmt_pix_type {
	RKISP1_FMT_YUV,
	RKISP1_FMT_RGB,
	RKISP1_FMT_BAYER,
	RKISP1_FMT_JPEG,
};

enum rkisp1_fmt_raw_pat_type {
	RKISP1_RAW_RGGB = 0,
	RKISP1_RAW_GRBG,
	RKISP1_RAW_GBRG,
	RKISP1_RAW_BGGR,
};

enum rkisp1_isp_pad {
	RKISP1_ISP_PAD_SINK_VIDEO,
	RKISP1_ISP_PAD_SINK_PARAMS,
	RKISP1_ISP_PAD_SOURCE_VIDEO,
	RKISP1_ISP_PAD_SOURCE_STATS,
	RKISP1_ISP_PAD_MAX
};

/*
 * struct rkisp1_sensor_async - Sensor information
 * @mbus: media bus configuration
 */
struct rkisp1_sensor_async {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	unsigned int lanes;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *pixel_rate_ctrl;
	struct phy *dphy;
};

/*
 * struct rkisp1_isp - ISP sub-device
 *
 * See Cropping regions of ISP in rkisp1.c for details
 * @sink_frm: input size, don't have to be equal to sensor size
 * @sink_fmt: input format
 * @sink_crop: crop for sink pad
 * @src_fmt: output format
 * @src_crop: output size
 *
 * @is_dphy_errctrl_disabled : if dphy errctrl is disabled (avoid endless interrupt)
 * @frame_sequence: used to synchronize frame_id between video devices.
 * @quantization: output quantization
 */
struct rkisp1_isp {
	struct v4l2_subdev sd;
	struct media_pad pads[RKISP1_ISP_PAD_MAX];
	struct v4l2_subdev_pad_config pad_cfg[RKISP1_ISP_PAD_MAX];
	const struct rkisp1_isp_mbus_info *sink_fmt;
	const struct rkisp1_isp_mbus_info *src_fmt;
	bool is_dphy_errctrl_disabled;
	atomic_t frame_sequence;
};

struct rkisp1_vdev_node {
	struct vb2_queue buf_queue;
	struct mutex vlock; /* ioctl serialization mutex */
	struct video_device vdev;
	struct media_pad pad;
};

struct rkisp1_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

struct rkisp1_dummy_buffer {
	void *vaddr;
	dma_addr_t dma_addr;
	u32 size;
};

struct rkisp1_device;

struct rkisp1_debug {
	struct dentry *debugfs_dir;
	unsigned long data_loss;
	unsigned long pic_size_error;
	unsigned long mipi_error;
};

/*
 * struct rkisp1_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @isp: ISP sub-device
 */
struct rkisp1_device {
	void __iomem *base_addr;
	int irq;
	struct device *dev;
	unsigned int clk_size;
	struct clk_bulk_data clks[RKISP1_MAX_BUS_CLK];
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct rkisp1_sensor_async *active_sensor;
	struct rkisp1_isp isp;
	struct media_pipeline pipe;
	struct vb2_alloc_ctx *alloc_ctx;
	struct rkisp1_debug debug;
};

/*
 * struct rkisp1_isp_mbus_info - ISP pad format info
 *
 * Translate mbus_code to hardware format values
 *
 * @bus_width: used for parallel
 */
struct rkisp1_isp_mbus_info {
	u32 mbus_code;
	enum rkisp1_fmt_pix_type fmt_type;
	u32 mipi_dt;
	u32 yuv_seq;
	u8 bus_width;
	enum rkisp1_fmt_raw_pat_type bayer_pat;
	unsigned int direction;
};

static inline void
rkisp1_write(struct rkisp1_device *rkisp1, u32 val, unsigned int addr)
{
	writel(val, rkisp1->base_addr + addr);
}

static inline u32 rkisp1_read(struct rkisp1_device *rkisp1, unsigned int addr)
{
	return readl(rkisp1->base_addr + addr);
}

void rkisp1_sd_adjust_crop_rect(struct v4l2_rect *crop,
				const struct v4l2_rect *bounds);

void rkisp1_sd_adjust_crop(struct v4l2_rect *crop,
			   const struct v4l2_mbus_framefmt *bounds);

int rkisp1_isp_register(struct rkisp1_device *rkisp1,
			struct v4l2_device *v4l2_dev);
void rkisp1_isp_unregister(struct rkisp1_device *rkisp1);

const struct rkisp1_isp_mbus_info *rkisp1_isp_mbus_info_get(u32 mbus_code);

void rkisp1_isp_isr(struct rkisp1_device *rkisp1);
void rkisp1_mipi_isr(struct rkisp1_device *rkisp1);

#endif /* _RKISP1_COMMON_H */
