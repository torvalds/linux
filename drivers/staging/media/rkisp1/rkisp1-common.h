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
#include "uapi/rkisp1-config.h"

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

enum rkisp1_rsz_pad {
	RKISP1_RSZ_PAD_SINK,
	RKISP1_RSZ_PAD_SRC,
};

enum rkisp1_stream_id {
	RKISP1_MAINPATH,
	RKISP1_SELFPATH,
};

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
 * @ops_lock: ops serialization
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
	struct mutex ops_lock;
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

/*
 * struct rkisp1_capture - ISP capture video device
 *
 * @pix.fmt: buffer format
 * @pix.info: pixel information
 * @pix.cfg: pixel configuration
 *
 * @buf.lock: lock to protect buf_queue
 * @buf.queue: queued buffer list
 * @buf.dummy: dummy space to store dropped data
 *
 * rkisp1 use shadowsock registers, so it need two buffer at a time
 * @buf.curr: the buffer used for current frame
 * @buf.next: the buffer used for next frame
 */
struct rkisp1_capture {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;
	enum rkisp1_stream_id id;
	struct rkisp1_capture_ops *ops;
	const struct rkisp1_capture_config *config;
	bool is_streaming;
	bool is_stopping;
	wait_queue_head_t done;
	unsigned int sp_y_stride;
	struct {
		/* protects queue, curr and next */
		spinlock_t lock;
		struct list_head queue;
		struct rkisp1_dummy_buffer dummy;
		struct rkisp1_buffer *curr;
		struct rkisp1_buffer *next;
	} buf;
	struct {
		const struct rkisp1_capture_fmt_cfg *cfg;
		const struct v4l2_format_info *info;
		struct v4l2_pix_format_mplane fmt;
	} pix;
};

/*
 * struct rkisp1_stats - ISP Statistics device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkisp1_stats {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;

	spinlock_t irq_lock;
	struct list_head stat;
	struct v4l2_format vdev_fmt;
	bool is_streaming;

	struct workqueue_struct *readout_wq;
	struct mutex wq_lock;
};

/*
 * struct rkisp1_params - ISP input parameters device
 *
 * @cur_params: Current ISP parameters
 * @is_first_params: the first params should take effect immediately
 */
struct rkisp1_params {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;

	spinlock_t config_lock;
	struct list_head params;
	struct rkisp1_params_cfg cur_params;
	struct v4l2_format vdev_fmt;
	bool is_streaming;
	bool is_first_params;

	enum v4l2_quantization quantization;
	enum rkisp1_fmt_raw_pat_type raw_type;
};

struct rkisp1_resizer {
	struct v4l2_subdev sd;
	enum rkisp1_stream_id id;
	struct rkisp1_device *rkisp1;
	struct media_pad pads[RKISP1_ISP_PAD_MAX];
	struct v4l2_subdev_pad_config pad_cfg[RKISP1_ISP_PAD_MAX];
	const struct rkisp1_rsz_config *config;
	enum rkisp1_fmt_pix_type fmt_type;
	struct mutex ops_lock;
};

struct rkisp1_debug {
	struct dentry *debugfs_dir;
	unsigned long data_loss;
	unsigned long pic_size_error;
	unsigned long mipi_error;
	unsigned long stats_error;
	unsigned long stop_timeout[2];
	unsigned long frame_drop[2];
};

/*
 * struct rkisp1_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @isp: ISP sub-device
 * @rkisp1_capture: capture video device
 * @stats: ISP statistics output device
 * @params: ISP input parameters device
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
	struct rkisp1_resizer resizer_devs[2];
	struct rkisp1_capture capture_devs[2];
	struct rkisp1_stats stats;
	struct rkisp1_params params;
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
void rkisp1_capture_isr(struct rkisp1_device *rkisp1);
void rkisp1_stats_isr(struct rkisp1_stats *stats, u32 isp_ris);
void rkisp1_params_isr(struct rkisp1_device *rkisp1, u32 isp_mis);

int rkisp1_capture_devs_register(struct rkisp1_device *rkisp1);
void rkisp1_capture_devs_unregister(struct rkisp1_device *rkisp1);

int rkisp1_resizer_devs_register(struct rkisp1_device *rkisp1);
void rkisp1_resizer_devs_unregister(struct rkisp1_device *rkisp1);

int rkisp1_stats_register(struct rkisp1_stats *stats,
			  struct v4l2_device *v4l2_dev,
			  struct rkisp1_device *rkisp1);
void rkisp1_stats_unregister(struct rkisp1_stats *stats);

void rkisp1_params_configure(struct rkisp1_params *params,
			     enum rkisp1_fmt_raw_pat_type bayer_pat,
			     enum v4l2_quantization quantization);
void rkisp1_params_disable(struct rkisp1_params *params);
int rkisp1_params_register(struct rkisp1_params *params,
			   struct v4l2_device *v4l2_dev,
			   struct rkisp1_device *rkisp1);
void rkisp1_params_unregister(struct rkisp1_params *params);

void rkisp1_params_isr_handler(struct rkisp1_device *rkisp1, u32 isp_mis);

#endif /* _RKISP1_COMMON_H */
