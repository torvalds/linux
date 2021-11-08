// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Dingxian Wen <shawn.wen@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/workqueue.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include "rk_hdmirx.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-3)");

#define	RK_HDMIRX_DRVNAME		"rk_hdmirx"
#define EDID_NUM_BLOCKS_MAX		2
#define EDID_BLOCK_SIZE			128
#define HDMIRX_DEFAULT_TIMING		V4L2_DV_BT_CEA_640X480P59_94
#define HDMIRX_VDEV_NAME		"stream_hdmirx"
#define HDMIRX_REQ_BUFS_MIN		2
#define HDMIRX_STORED_BIT_WIDTH		8
#define IREF_CLK_FREQ_HZ		428571429
#define MEMORY_ALIGN_ROUND_UP_BYTES	64
#define HDMIRX_PLANE_Y			0
#define HDMIRX_PLANE_CBCR		1

enum hdmirx_pix_fmt {
	HDMIRX_RGB888 = 0,
	HDMIRX_YUV422 = 1,
	HDMIRX_YUV444 = 2,
	HDMIRX_YUV420 = 3,
};

static const char * const pix_fmt_str[] = {
	"RGB888",
	"YUV422",
	"YUV444",
	"YUV420",
};

enum ddr_store_fmt {
	STORE_RGB888 = 0,
	STORE_RGBA_ARGB,
	STORE_YUV420_8BIT,
	STORE_YUV420_10BIT,
	STORE_YUV422_8BIT,
	STORE_YUV422_10BIT,
	STORE_YUV444_8BIT,
	STORE_YUV420_16BIT = 8,
	STORE_YUV422_16BIT = 9,
};

struct hdmirx_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

struct hdmirx_output_fmt {
	u32 fourcc;
	u8 cplanes;
	u8 mplanes;
	u8 bpp[VIDEO_MAX_PLANES];
};

struct hdmirx_stream {
	struct rk_hdmirx_dev *hdmirx_dev;
	struct video_device vdev;
	struct vb2_queue buf_queue;
	struct list_head buf_head;
	struct hdmirx_buffer *curr_buf;
	struct hdmirx_buffer *next_buf;
	struct v4l2_pix_format_mplane pixm;
	const struct hdmirx_output_fmt *out_fmt;
	struct mutex vlock;
	spinlock_t vbq_lock;
	bool stopping;
	wait_queue_head_t wq_stopped;
	u32 frame_idx;
	u32 dma_idle_cnt;
	u32 irq_stat;
};

struct rk_hdmirx_dev {
	struct device *dev;
	struct device_node *of_node;
	struct hdmirx_stream stream;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_dv_timings timings;
	struct gpio_desc *hdmirx_det_gpio;
	struct delayed_work delayed_work_hotplug;
	struct delayed_work delayed_work_res_change;
	struct mutex stream_lock;
	struct mutex work_lock;
	struct reset_control *reset;
	struct clk_bulk_data *clks;
	struct regmap *grf;
	struct regmap *vo1_grf;
	void __iomem *regs;
	int hdmi_irq;
	int dma_irq;
	int det_irq;
	enum hdmirx_pix_fmt pix_fmt;
	bool avi_pkt_rcv;
	bool cr_write_done;
	bool cr_read_done;
	bool timer_base_lock;
	bool tmds_clk_ratio;
	bool is_dvi_mode;
	bool power_on;
	u32 num_clks;
	u32 edid_blocks_written;
	u32 hpd_trigger_level;
	u32 cur_vic;
	u32 cur_fmt_fourcc;
	u32 color_depth;
};

static bool tx_5v_power_present(struct rk_hdmirx_dev *hdmirx_dev);
static void hdmirx_set_fmt(struct hdmirx_stream *stream,
		struct v4l2_pix_format_mplane *pixm, bool try);

static u8 edid_init_data[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x49, 0x70, 0x88, 0x35, 0x01, 0x00, 0x00, 0x00,
	0x2D, 0x1F, 0x01, 0x03, 0x80, 0x78, 0x44, 0x78,
	0x0A, 0xCF, 0x74, 0xA3, 0x57, 0x4C, 0xB0, 0x23,
	0x09, 0x48, 0x4C, 0x21, 0x08, 0x00, 0x61, 0x40,
	0x01, 0x01, 0x81, 0x00, 0x95, 0x00, 0xA9, 0xC0,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x08, 0xE8,
	0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80, 0xB0, 0x58,
	0x8A, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E,
	0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40,
	0x58, 0x2C, 0x45, 0x00, 0xB9, 0xA8, 0x42, 0x00,
	0x00, 0x1E, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x52,
	0x4B, 0x2D, 0x55, 0x48, 0x44, 0x0A, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFD,
	0x00, 0x3B, 0x46, 0x1F, 0x8C, 0x3C, 0x00, 0x0A,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xA3,

	0x02, 0x03, 0x36, 0xD2, 0x51, 0x07, 0x16, 0x14,
	0x05, 0x01, 0x03, 0x12, 0x13, 0x84, 0x22, 0x1F,
	0x90, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x23, 0x09,
	0x07, 0x07, 0x83, 0x01, 0x00, 0x00, 0x66, 0x03,
	0x0C, 0x00, 0x30, 0x00, 0x10, 0x67, 0xD8, 0x5D,
	0xC4, 0x01, 0x78, 0xC8, 0x07, 0xE3, 0x05, 0x03,
	0x01, 0xE4, 0x0F, 0x00, 0xF0, 0x01, 0x08, 0xE8,
	0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80, 0xB0, 0x58,
	0x8A, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E,
	0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40,
	0x58, 0x2C, 0x45, 0x00, 0xB9, 0xA8, 0x42, 0x00,
	0x00, 0x9E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB5,
};

static const struct v4l2_dv_timings_cap hdmirx_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(640, 4096,			/* min/max width */
			     480, 2160,			/* min/max height */
			     20000000, 600000000,	/* min/max pixelclock */
			     /* standards */
			     V4L2_DV_BT_STD_CEA861,
			     /* capabilities */
			     V4L2_DV_BT_CAP_PROGRESSIVE |
			     V4L2_DV_BT_CAP_INTERLACED)
};

static const struct hdmirx_output_fmt g_out_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 24 },
	}, {
		.fourcc = V4L2_PIX_FMT_NV24,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}
};

static inline struct hdmirx_buffer *to_hdmirx_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct hdmirx_buffer, vb);
}

static inline void hdmirx_writel(struct rk_hdmirx_dev *hdmirx_dev, int reg, u32 val)
{
	writel(val, hdmirx_dev->regs + reg);
}

static inline u32 hdmirx_readl(struct rk_hdmirx_dev *hdmirx_dev, int reg)
{
	return readl(hdmirx_dev->regs + reg);
}

static void hdmirx_update_bits(struct rk_hdmirx_dev *hdmirx_dev, int reg, u32 mask,
		u32 data)
{
	u32 val = hdmirx_readl(hdmirx_dev, reg) & ~mask;

	val |= (data & mask);
	hdmirx_writel(hdmirx_dev, reg, val);
}

static int hdmirx_subscribe_event(struct v4l2_fh *fh,
			const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		if (fh->vdev->vfl_dir == VFL_DIR_RX)
			return v4l2_src_change_event_subscribe(fh, sub);
		break;
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);

	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}

	return -EINVAL;
}

static bool port_no_link(struct rk_hdmirx_dev *hdmirx_dev)
{
	return !tx_5v_power_present(hdmirx_dev);
}

static bool signal_not_lock(struct rk_hdmirx_dev *hdmirx_dev)
{
	u32 mu_status, dma_st10, cmu_st;

	mu_status = hdmirx_readl(hdmirx_dev, MAINUNIT_STATUS);
	dma_st10 = hdmirx_readl(hdmirx_dev, DMA_STATUS10);
	cmu_st = hdmirx_readl(hdmirx_dev, CMU_STATUS);

	if ((mu_status & TMDSVALID_STABLE_ST) &&
	    (dma_st10 & HDMIRX_LOCK) &&
	    (cmu_st & TMDSQPCLK_LOCKED_ST))
		return false;

	return true;
}

static bool tx_5v_power_present(struct rk_hdmirx_dev *hdmirx_dev)
{
	bool ret;
	int val, i, cnt;

	cnt = 0;
	for (i = 0; i < 10; i++) {
		usleep_range(1000, 1100);
		val = gpiod_get_value(hdmirx_dev->hdmirx_det_gpio);
		if (val > 0)
			cnt++;
		if (cnt >= 7)
			break;
	}

	ret = (cnt >= 7) ? true : false;
	v4l2_dbg(3, debug, &hdmirx_dev->v4l2_dev, "%s: %d\n", __func__, ret);

	return ret;
}

static int hdmirx_g_dv_timings(struct file *file, void *_fh,
				 struct v4l2_dv_timings *timings)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	u32 dma_cfg1;

	*timings = hdmirx_dev->timings;
	dma_cfg1 = hdmirx_readl(hdmirx_dev, DMA_CONFIG1);
	v4l2_dbg(1, debug, v4l2_dev, "%s: pix_fmt: %s, DMA_CONFIG1:%#x\n",
			__func__, pix_fmt_str[hdmirx_dev->pix_fmt], dma_cfg1);

	return 0;
}

static int hdmirx_s_dv_timings(struct file *file, void *_fh,
				 struct v4l2_dv_timings *timings)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(hdmirx_dev->v4l2_dev.name,
				"hdmirx_s_dv_timings: ", timings, false);

	if (!v4l2_valid_dv_timings(timings, &hdmirx_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, v4l2_dev,
				"%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	/* Check if the timings are part of the CEA-861 timings. */
	if (!v4l2_find_dv_timings_cap(timings, &hdmirx_timings_cap,
				      0, NULL, NULL))
		return -EINVAL;

	if (v4l2_match_dv_timings(&hdmirx_dev->timings, timings, 0, false)) {
		v4l2_dbg(1, debug, v4l2_dev, "%s: no change\n", __func__);
		return 0;
	}

	/*
	 * Changing the timings implies a format change, which is not allowed
	 * while buffers for use with streaming have already been allocated.
	 */
	if (vb2_is_busy(&stream->buf_queue))
		return -EBUSY;

	hdmirx_dev->timings = *timings;
	/* Update the internal format */
	hdmirx_set_fmt(stream, &stream->pixm, false);

	return 0;
}

static void hdmirx_get_colordepth(struct rk_hdmirx_dev *hdmirx_dev)
{
	u32 val, color_depth_reg;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	val = hdmirx_readl(hdmirx_dev, DMA_STATUS11);
	color_depth_reg = (val & HDMIRX_COLOR_DEPTH_MASK) >> 3;

	switch (color_depth_reg) {
	case 0x4:
		hdmirx_dev->color_depth = 24;
		break;
	case 0x5:
		hdmirx_dev->color_depth = 30;
		break;
	case 0x6:
		hdmirx_dev->color_depth = 36;
		break;
	case 0x7:
		hdmirx_dev->color_depth = 48;
		break;

	default:
		hdmirx_dev->color_depth = 24;
		break;
	}

	v4l2_dbg(1, debug, v4l2_dev, "%s: color_depth: %d, reg_val:%d\n",
			__func__, hdmirx_dev->color_depth, color_depth_reg);
}

static void hdmirx_get_pix_fmt(struct rk_hdmirx_dev *hdmirx_dev)
{
	u32 val;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	val = hdmirx_readl(hdmirx_dev, DMA_STATUS11);
	hdmirx_dev->pix_fmt = val & HDMIRX_FORMAT_MASK;

	switch (hdmirx_dev->pix_fmt) {
	case HDMIRX_RGB888:
		hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_RGB24;
		break;
	case HDMIRX_YUV422:
		hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_NV16;
		break;
	case HDMIRX_YUV444:
		hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_NV24;
		break;
	case HDMIRX_YUV420:
		hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_NV12;
		break;

	default:
		v4l2_err(v4l2_dev,
			"%s: err pix_fmt: %d, set RGB888 as default\n",
			__func__, hdmirx_dev->pix_fmt);
		hdmirx_dev->pix_fmt = HDMIRX_RGB888;
		hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_RGB24;
		break;
	}

	v4l2_dbg(1, debug, v4l2_dev, "%s: pix_fmt: %s\n", __func__,
			pix_fmt_str[hdmirx_dev->pix_fmt]);
}

static int hdmirx_get_detected_timings(struct rk_hdmirx_dev *hdmirx_dev,
		struct v4l2_dv_timings *timings)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 hact, vact, htotal, vtotal, fps;
	u32 hfp, hs, hbp, vfp, vs, vbp;
	u32 field_type, color_depth, deframer_st;
	u32 val, tmdsqpclk_freq, pix_clk;
	u64 tmp_data, tmds_clk;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	timings->type = V4L2_DV_BT_656_1120;

	val = hdmirx_readl(hdmirx_dev, DMA_STATUS11);
	field_type = (val & HDMIRX_TYPE_MASK) >> 7;
	hdmirx_get_pix_fmt(hdmirx_dev);
	bt->interlaced = field_type & BIT(0) ?
		V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;
	val = hdmirx_readl(hdmirx_dev, PKTDEC_AVIIF_PB7_4);
	hdmirx_dev->cur_vic =  val | VIC_VAL_MASK;
	hdmirx_get_colordepth(hdmirx_dev);
	color_depth = hdmirx_dev->color_depth;
	deframer_st = hdmirx_readl(hdmirx_dev, DEFRAMER_STATUS);
	hdmirx_dev->is_dvi_mode = deframer_st & OPMODE_STS_MASK ? false : true;
	tmdsqpclk_freq = hdmirx_readl(hdmirx_dev, CMU_TMDSQPCLK_FREQ);
	tmds_clk = tmdsqpclk_freq * 4 * 1000U;
	tmp_data = tmds_clk * 24;
	do_div(tmp_data, color_depth);
	pix_clk = tmp_data;

	val = hdmirx_readl(hdmirx_dev, DMA_STATUS2);
	hact = (val >> 16) & 0xffff;
	vact = val & 0xffff;
	val = hdmirx_readl(hdmirx_dev, DMA_STATUS3);
	htotal = (val >> 16) & 0xffff;
	vtotal = val & 0xffff;
	val = hdmirx_readl(hdmirx_dev, DMA_STATUS4);
	hs = (val >> 16) & 0xffff;
	vs = val & 0xffff;
	val = hdmirx_readl(hdmirx_dev, DMA_STATUS5);
	hbp = (val >> 16) & 0xffff;
	vbp = val & 0xffff;

	if (hdmirx_dev->pix_fmt == HDMIRX_YUV420)
		htotal *= 2;

	hfp = htotal - hact - hs - hbp;
	vfp = vtotal - vact - vs - vbp;
	fps = (pix_clk + (htotal * vtotal) / 2) / (htotal * vtotal);

	bt->width = hact;
	bt->height = vact;
	bt->hfrontporch = hfp;
	bt->hsync = hs;
	bt->hbackporch = hbp;
	bt->vfrontporch = vfp;
	bt->vsync = vs;
	bt->vbackporch = vbp;
	bt->pixelclock = pix_clk;

	if (bt->interlaced == V4L2_DV_INTERLACED) {
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
		bt->pixelclock /= 2;
	}

	v4l2_dbg(1, debug, v4l2_dev,
			"act:%dx%d, total:%dx%d, fps:%d, pixclk:%llu\n",
			hact, bt->height, htotal, vtotal, fps, bt->pixelclock);
	v4l2_dbg(2, debug, v4l2_dev,
			"hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d\n",
			bt->hfrontporch, bt->hsync, bt->hbackporch,
			bt->vfrontporch, bt->vsync, bt->vbackporch);
	v4l2_dbg(2, debug, v4l2_dev, "tmds_clk:%lld\n", tmds_clk);
	v4l2_dbg(1, debug, v4l2_dev,
			"interlace:%d, fmt:%d, vic:%d, color:%d, mode:%s\n",
			bt->interlaced, hdmirx_dev->pix_fmt,
			hdmirx_dev->cur_vic, hdmirx_dev->color_depth,
			hdmirx_dev->is_dvi_mode ? "dvi" : "hdmi");
	v4l2_dbg(2, debug, v4l2_dev, "deframer_st:%#x\n", deframer_st);

	return 0;
}

static int hdmirx_query_dv_timings(struct file *file, void *_fh,
		struct v4l2_dv_timings *timings)
{
	int ret;
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	if (port_no_link(hdmirx_dev)) {
		v4l2_err(v4l2_dev, "%s port has no link!\n", __func__);
		return -ENOLINK;
	}

	if (signal_not_lock(hdmirx_dev)) {
		v4l2_err(v4l2_dev, "%s signal is not locked!\n", __func__);
		return -ENOLCK;
	}

	ret = hdmirx_get_detected_timings(hdmirx_dev, timings);
	if (ret)
		return ret;

	if (debug)
		v4l2_print_dv_timings(hdmirx_dev->v4l2_dev.name,
				"query_dv_timings: ", timings, false);

	if (!v4l2_valid_dv_timings(timings, &hdmirx_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, v4l2_dev, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	return 0;
}

static void hdmirx_hpd_ctrl(struct rk_hdmirx_dev *hdmirx_dev, bool en)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "%s: %sable, hpd_trigger_level:%d\n",
			__func__, en ? "en" : "dis",
			hdmirx_dev->hpd_trigger_level);
	hdmirx_update_bits(hdmirx_dev, SCDC_CONFIG, HPDLOW, en ? 0 : HPDLOW);
	en = hdmirx_dev->hpd_trigger_level ? en : !en;
	hdmirx_writel(hdmirx_dev, CORE_CONFIG, en);
}

static int hdmirx_write_edid(struct rk_hdmirx_dev *hdmirx_dev,
		struct v4l2_edid *edid, bool hpd_up)
{
	u32 i;
	u32 edid_len = edid->blocks * EDID_BLOCK_SIZE;
	struct device *dev = hdmirx_dev->dev;
	char data[300];

	memset(edid->reserved, 0, sizeof(edid->reserved));
	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block != 0)
		return -EINVAL;

	if (edid->blocks > EDID_NUM_BLOCKS_MAX) {
		edid->blocks = EDID_NUM_BLOCKS_MAX;
		return -E2BIG;
	}

	if (edid->blocks == 0) {
		hdmirx_dev->edid_blocks_written = 0;
		return 0;
	}

	hdmirx_hpd_ctrl(hdmirx_dev, false);
	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG11,
			EDID_READ_EN_MASK |
			EDID_WRITE_EN_MASK |
			EDID_SLAVE_ADDR_MASK,
			EDID_READ_EN(0) |
			EDID_WRITE_EN(1) |
			EDID_SLAVE_ADDR(0x50));
	for (i = 0; i < edid_len; i++)
		hdmirx_writel(hdmirx_dev, DMA_CONFIG10, edid->edid[i]);

	/* read out for debug */
	if (debug >= 2) {
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG11,
				EDID_READ_EN_MASK |
				EDID_WRITE_EN_MASK,
				EDID_READ_EN(1) |
				EDID_WRITE_EN(0));
		dev_info(dev, "%s: Read EDID: ======\n", __func__);
		edid_len = edid_len > sizeof(data) ? sizeof(data) : edid_len;
		memset(data, 0, sizeof(data));
		for (i = 0; i < edid_len; i++)
			data[i] = hdmirx_readl(hdmirx_dev, DMA_STATUS14);

		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1, data,
				edid_len, false);
	}

	/*
	 * You must set EDID_READ_EN & EDID_WRITE_EN bit to 0,
	 * when the read/write edid operation is completed.Otherwise, it
	 * will affect the reading and writing of other registers
	 */
	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG11,
			EDID_READ_EN_MASK |
			EDID_WRITE_EN_MASK,
			EDID_READ_EN(0) |
			EDID_WRITE_EN(0));

	hdmirx_dev->edid_blocks_written = edid->blocks;
	if (hpd_up) {
		if (tx_5v_power_present(hdmirx_dev))
			hdmirx_hpd_ctrl(hdmirx_dev, true);
	}

	return 0;
}

static int hdmirx_set_edid(struct file *file, void *fh,
		struct v4l2_edid *edid)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;

	return hdmirx_write_edid(hdmirx_dev, edid, true);
}

static int hdmirx_get_edid(struct file *file, void *fh,
		struct v4l2_edid *edid)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	u32 i;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = hdmirx_dev->edid_blocks_written;
		return 0;
	}

	if (hdmirx_dev->edid_blocks_written == 0)
		return -ENODATA;

	if (edid->start_block >= hdmirx_dev->edid_blocks_written ||
			edid->blocks == 0)
		return -EINVAL;

	if (edid->start_block + edid->blocks > hdmirx_dev->edid_blocks_written)
		edid->blocks = hdmirx_dev->edid_blocks_written - edid->start_block;

	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG11,
			EDID_READ_EN_MASK |
			EDID_WRITE_EN_MASK,
			EDID_READ_EN(1) |
			EDID_WRITE_EN(0));

	for (i = 0; i < (edid->blocks * EDID_BLOCK_SIZE); i++)
		edid->edid[i] = hdmirx_readl(hdmirx_dev, DMA_STATUS14);

	v4l2_dbg(1, debug, v4l2_dev, "%s: Read EDID: =====\n", __func__);
	if (debug > 0)
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1,
			edid->edid, edid->blocks * EDID_BLOCK_SIZE, false);

	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG11,
			EDID_READ_EN_MASK |
			EDID_WRITE_EN_MASK,
			EDID_READ_EN(0) |
			EDID_WRITE_EN(0));

	return 0;
}

static int hdmirx_dv_timings_cap(struct file *file, void *fh,
				   struct v4l2_dv_timings_cap *cap)
{
	*cap = hdmirx_timings_cap;

	return 0;
}

static int hdmirx_enum_dv_timings(struct file *file, void *_fh,
				    struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &hdmirx_timings_cap, NULL, NULL);
}

static void hdmirx_scdc_init(struct rk_hdmirx_dev *hdmirx_dev)
{
	hdmirx_update_bits(hdmirx_dev, I2C_SLAVE_CONFIG1,
			   I2C_SDA_OUT_HOLD_VALUE_QST_MASK |
			   I2C_SDA_IN_HOLD_VALUE_QST_MASK,
			   I2C_SDA_OUT_HOLD_VALUE_QST(0x80) |
			   I2C_SDA_IN_HOLD_VALUE_QST(0x15));
	hdmirx_update_bits(hdmirx_dev, SCDC_REGBANK_CONFIG0,
			   SCDC_SINKVERSION_QST_MASK,
			   SCDC_SINKVERSION_QST(1));
}

static int wait_reg_bit_status(struct rk_hdmirx_dev *hdmirx_dev,
		u32 reg, u32 bit_mask, u32 expect_val, bool is_grf, u32 ms)
{
	u32 i, val;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	for (i = 0; i < ms; i++) {
		if (is_grf)
			regmap_read(hdmirx_dev->grf, reg, &val);
		else
			val = hdmirx_readl(hdmirx_dev, reg);

		if ((val & bit_mask) == expect_val) {
			v4l2_dbg(2, debug, v4l2_dev,
				"%s:  i:%d, time: %dms\n", __func__, i, ms);
			break;
		}

		usleep_range(1000, 1010);
	}

	if (i == ms)
		return -1;

	return 0;
}

static int hdmirx_phy_register_write(struct rk_hdmirx_dev *hdmirx_dev,
		u32 phy_reg, u32 val)
{
	u32 i;
	struct device *dev = hdmirx_dev->dev;

	hdmirx_dev->cr_write_done = false;
	/* clear irq status */
	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_CLEAR, 0xffffffff);
	/* en irq */
	hdmirx_update_bits(hdmirx_dev, MAINUNIT_2_INT_MASK_N,
			PHYCREG_CR_WRITE_DONE, PHYCREG_CR_WRITE_DONE);
	/* write phy reg addr */
	hdmirx_writel(hdmirx_dev, PHYCREG_CONFIG1, phy_reg);
	/* write phy reg val */
	hdmirx_writel(hdmirx_dev, PHYCREG_CONFIG2, val);
	/* config write enable */
	hdmirx_writel(hdmirx_dev, PHYCREG_CONTROL, PHYCREG_CR_PARA_WRITE_P);

	for (i = 0; i < 50; i++) {
		usleep_range(200, 210);
		if (hdmirx_dev->cr_write_done)
			break;
	}

	if (i == 50) {
		dev_err(dev, "%s wait cr write done failed!\n", __func__);
		return -1;
	}

	return 0;
}

static void hdmirx_phy_config(struct rk_hdmirx_dev *hdmirx_dev)
{
	struct device *dev = hdmirx_dev->dev;

	hdmirx_writel(hdmirx_dev, SCDC_INT_CLEAR, 0xffffffff);
	hdmirx_update_bits(hdmirx_dev, SCDC_INT_MASK_N, SCDCTMDSCCFG_CHG,
				SCDCTMDSCCFG_CHG);
	/* cr_para_clk 24M */
	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, REFFREQ_SEL_MASK, REFFREQ_SEL(0));
	/* rx data width 40bit valid */
	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, RXDATA_WIDTH, RXDATA_WIDTH);
	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, PHY_RESET, PHY_RESET);
	usleep_range(100, 110);
	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, PHY_RESET, 0);
	usleep_range(100, 110);
	/* select cr para interface */
	hdmirx_writel(hdmirx_dev, PHYCREG_CONFIG0, 0x3);

	if (wait_reg_bit_status(hdmirx_dev, SYS_GRF_SOC_STATUS1,
				HDMIRXPHY_SRAM_INIT_DONE,
				HDMIRXPHY_SRAM_INIT_DONE, true, 10))
		dev_err(dev, "%s phy SRAM init failed!\n", __func__);

	regmap_write(hdmirx_dev->grf, SYS_GRF_SOC_CON1,
		(HDMIRXPHY_SRAM_EXT_LD_DONE << 16) | HDMIRXPHY_SRAM_EXT_LD_DONE);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 2);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 3);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 2);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 2);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 3);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 2);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 0);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 1);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 0);
	hdmirx_phy_register_write(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, 0);

	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, PHY_PDDQ, 0);
	if (wait_reg_bit_status(hdmirx_dev, PHY_STATUS, PDDQ_ACK, 0, false, 10))
		dev_err(dev, "%s wait pddq ack failed!\n", __func__);

	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, HDMI_DISABLE, 0);
	if (wait_reg_bit_status(hdmirx_dev, PHY_STATUS, HDMI_DISABLE_ACK, 0,
				false, 50))
		dev_err(dev, "%s wait hdmi disable ack failed!\n", __func__);

	hdmirx_dev->tmds_clk_ratio = (hdmirx_readl(hdmirx_dev,
		SCDC_REGBANK_STATUS1) & SCDC_TMDSBITCLKRATIO) > 0;

	if (hdmirx_dev->tmds_clk_ratio) {
		dev_info(dev, "%s HDMITX greater than 3.4Gbps!\n", __func__);
		hdmirx_update_bits(hdmirx_dev, PHY_CONFIG,
				   TMDS_CLOCK_RATIO, TMDS_CLOCK_RATIO);
	} else {
		dev_info(dev, "%s HDMITX less than 3.4Gbps!\n", __func__);
		hdmirx_update_bits(hdmirx_dev, PHY_CONFIG,
				   TMDS_CLOCK_RATIO, 0);
	}
}

static void hdmirx_controller_init(struct rk_hdmirx_dev *hdmirx_dev)
{
	u32 i;
	struct device *dev = hdmirx_dev->dev;

	hdmirx_dev->timer_base_lock = false;
	hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_CLEAR, 0xffffffff);
	/* en irq */
	hdmirx_update_bits(hdmirx_dev, MAINUNIT_0_INT_MASK_N,
			TIMER_BASE_LOCKED_IRQ, TIMER_BASE_LOCKED_IRQ);
	/* write irefclk freq */
	hdmirx_writel(hdmirx_dev, GLOBAL_TIMER_REF_BASE, IREF_CLK_FREQ_HZ);
	for (i = 0; i < 50; i++) {
		usleep_range(200, 210);
		if (hdmirx_dev->timer_base_lock)
			break;
	}

	if (i == 50)
		dev_err(dev, "%s wait timer base lock failed!\n", __func__);

	hdmirx_update_bits(hdmirx_dev, CMU_CONFIG0,
			   TMDSQPCLK_STABLE_FREQ_MARGIN_MASK |
			   AUDCLK_STABLE_FREQ_MARGIN_MASK,
			   TMDSQPCLK_STABLE_FREQ_MARGIN(2) |
			   AUDCLK_STABLE_FREQ_MARGIN(1));
	hdmirx_update_bits(hdmirx_dev, DESCRAND_EN_CONTROL,
			   SCRAMB_EN_SEL_QST_MASK, SCRAMB_EN_SEL_QST(1));
	hdmirx_update_bits(hdmirx_dev, CED_CONFIG,
			   CED_VIDDATACHECKEN_QST|
			   CED_DATAISCHECKEN_QST |
			   CED_GBCHECKEN_QST |
			   CED_CTRLCHECKEN_QST |
			   CED_CHLOCKMAXER_QST_MASK,
			   CED_VIDDATACHECKEN_QST|
			   // CED_DATAISCHECKEN_QST |
			   CED_GBCHECKEN_QST |
			   CED_CTRLCHECKEN_QST |
			   CED_CHLOCKMAXER_QST(0x10));

	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6,
				VSYNC_TOGGLE_EN|
				HSYNC_TOGGLE_EN,
				VSYNC_TOGGLE_EN|
				HSYNC_TOGGLE_EN);

	hdmirx_update_bits(hdmirx_dev, VIDEO_CONFIG2,
				VPROC_VSYNC_POL_OVR_VALUE|
				VPROC_VSYNC_POL_OVR_EN|
				VPROC_HSYNC_POL_OVR_VALUE|
				VPROC_HSYNC_POL_OVR_EN,
				VPROC_VSYNC_POL_OVR_EN|
				VPROC_HSYNC_POL_OVR_EN);
}

static void hdmirx_format_change(struct rk_hdmirx_dev *hdmirx_dev)
{
	int i, cnt;
	u32 width, height;
	struct v4l2_dv_timings timings;
	struct v4l2_bt_timings *bt = &timings.bt;
	struct hdmirx_stream *stream = &hdmirx_dev->stream;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	const struct v4l2_event ev_src_chg = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	cnt = 0;
	width = 0;
	height = 0;
	for (i = 0; i < 20; i++) {
		hdmirx_get_detected_timings(hdmirx_dev, &timings);

		if ((width != bt->width) || (height != bt->height)) {
			width = bt->width;
			height = bt->height;
			cnt = 0;
		} else {
			cnt++;
		}

		if (cnt >= 8)
			break;

		usleep_range(10*1000, 10*1100);
	}

	if (cnt < 8)
		v4l2_dbg(1, debug, v4l2_dev, "%s: res not stable!\n", __func__);

	if (!v4l2_match_dv_timings(&hdmirx_dev->timings, &timings, 0, false)) {
		/* automatically set timing rather than set by userspace */
		hdmirx_dev->timings = timings;
		v4l2_print_dv_timings(hdmirx_dev->v4l2_dev.name,
				"hdmirx_format_change: New format: ",
				&timings, false);
	}

	v4l2_dbg(1, debug, v4l2_dev, "%s: queue res_chg_event\n", __func__);
	v4l2_event_queue(&stream->vdev, &ev_src_chg);
}

static void hdmirx_set_ddr_store_fmt(struct rk_hdmirx_dev *hdmirx_dev)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	enum ddr_store_fmt store_fmt;
	u32 dma_cfg1;

	switch (hdmirx_dev->pix_fmt) {
	case HDMIRX_RGB888:
		store_fmt = STORE_RGB888;
		break;
	case HDMIRX_YUV444:
		store_fmt = STORE_YUV444_8BIT;
		break;
	case HDMIRX_YUV422:
		store_fmt = STORE_YUV422_8BIT;
		break;
	case HDMIRX_YUV420:
		store_fmt = STORE_YUV420_8BIT;
		break;

	default:
		store_fmt = STORE_RGB888;
		break;
	}

	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG1,
			DDR_STORE_FORMAT_MASK, DDR_STORE_FORMAT(store_fmt));
	dma_cfg1 = hdmirx_readl(hdmirx_dev, DMA_CONFIG1);
	v4l2_dbg(1, debug, v4l2_dev, "%s: pix_fmt: %s, DMA_CONFIG1:%#x\n",
			__func__, pix_fmt_str[hdmirx_dev->pix_fmt], dma_cfg1);
}

static int hdmirx_wait_lock_and_get_timing(struct rk_hdmirx_dev *hdmirx_dev)
{
	u32 i;
	u32 mu_status, scdc_status, dma_st10, cmu_st;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	for (i = 0; i < 300; i++) {
		mu_status = hdmirx_readl(hdmirx_dev, MAINUNIT_STATUS);
		scdc_status = hdmirx_readl(hdmirx_dev, SCDC_REGBANK_STATUS3);
		dma_st10 = hdmirx_readl(hdmirx_dev, DMA_STATUS10);
		cmu_st = hdmirx_readl(hdmirx_dev, CMU_STATUS);

		if ((mu_status & TMDSVALID_STABLE_ST) &&
				(dma_st10 & HDMIRX_LOCK) &&
				(cmu_st & TMDSQPCLK_LOCKED_ST))
			break;

		if (!tx_5v_power_present(hdmirx_dev)) {
			v4l2_err(v4l2_dev, "%s HDMI pull out, return!\n", __func__);
			return -1;
		}
	}

	if (i == 300) {
		v4l2_err(v4l2_dev, "%s signal not lock!\n", __func__);
		v4l2_err(v4l2_dev, "%s mu_st:%#x, scdc_st:%#x, dma_st10:%#x\n",
				__func__, mu_status, scdc_status, dma_st10);

		return -1;
	}

	v4l2_info(v4l2_dev, "%s signal lock ok, i:%d!\n", __func__, i);
	hdmirx_writel(hdmirx_dev, GLOBAL_SWRESET_REQUEST, DATAPATH_SWRESETREQ);

	hdmirx_dev->avi_pkt_rcv = false;
	hdmirx_writel(hdmirx_dev, PKT_2_INT_CLEAR, 0xffffffff);
	hdmirx_update_bits(hdmirx_dev, PKT_2_INT_MASK_N,
			PKTDEC_AVIIF_RCV_IRQ, PKTDEC_AVIIF_RCV_IRQ);

	for (i = 0; i < 300; i++) {
		usleep_range(1000, 1100);
		if (hdmirx_dev->avi_pkt_rcv) {
			v4l2_dbg(1, debug, v4l2_dev,
					"%s: avi_pkt_rcv i:%d\n", __func__, i);
			break;
		}
	}

	if (i == 300) {
		v4l2_err(v4l2_dev, "%s wait avi_pkt_rcv failed!\n", __func__);
		hdmirx_update_bits(hdmirx_dev, PKT_2_INT_MASK_N,
				PKTDEC_AVIIF_RCV_IRQ, 0);
	}

	usleep_range(50*1000, 50*1010);
	hdmirx_format_change(hdmirx_dev);

	return 0;
}

static void hdmirx_dma_config(struct rk_hdmirx_dev *hdmirx_dev)
{
	hdmirx_set_ddr_store_fmt(hdmirx_dev);

	/* Note: uv_swap, rb can not swap, doc err*/
	if (hdmirx_dev->cur_fmt_fourcc != V4L2_PIX_FMT_NV16)
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6, RB_SWAP_EN, RB_SWAP_EN);
	else
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6, RB_SWAP_EN, 0);

	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG7,
				LOCK_FRAME_NUM_MASK,
				LOCK_FRAME_NUM(2));
	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG1,
				UV_WID_MASK |
				Y_WID_MASK |
				ABANDON_EN,
				UV_WID(1) |
				Y_WID(2) |
				ABANDON_EN);
}

static void hdmirx_submodule_init(struct rk_hdmirx_dev *hdmirx_dev)
{
	/* Note: if not config HDCP2_CONFIG, there will be some errors; */
	hdmirx_writel(hdmirx_dev, HDCP2_CONFIG, 0x2);
	hdmirx_scdc_init(hdmirx_dev);
	hdmirx_controller_init(hdmirx_dev);
}

static int hdmirx_enum_input(struct file *file, void *priv,
			    struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = 0;
	strscpy(input->name, "hdmirx", sizeof(input->name));
	input->capabilities = V4L2_IN_CAP_DV_TIMINGS;

	return 0;
}

static int fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
{
	/* Note: cbcr plane bpp is 16 bit */
	switch (fcc) {
	case V4L2_PIX_FMT_NV24:
		*xsubs = 1;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV16:
		*xsubs = 2;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV12:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static u32 hdmirx_align_bits_per_pixel(const struct hdmirx_output_fmt *fmt,
				      int plane_index)
{
	u32 bpp = 0;

	if (fmt) {
		switch (fmt->fourcc) {
		case V4L2_PIX_FMT_NV24:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_RGB24:
			bpp = fmt->bpp[plane_index];
			break;

		default:
			pr_err("fourcc: %#x is not supported!\n", fmt->fourcc);
			break;
		}
	}

	return bpp;
}

static const struct
hdmirx_output_fmt *find_output_fmt(struct hdmirx_stream *stream, u32 pixelfmt)
{
	const struct hdmirx_output_fmt *fmt;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(g_out_fmts); i++) {
		fmt = &g_out_fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}

	return NULL;
}

static void hdmirx_set_fmt(struct hdmirx_stream *stream,
			  struct v4l2_pix_format_mplane *pixm,
			  bool try)
{
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct v4l2_bt_timings *bt = &hdmirx_dev->timings.bt;
	const struct hdmirx_output_fmt *fmt;
	unsigned int imagesize = 0, planes;
	u32 xsubs = 1, ysubs = 1, i;

	memset(&pixm->plane_fmt[0], 0, sizeof(struct v4l2_plane_pix_format));
	fmt = find_output_fmt(stream, pixm->pixelformat);
	if (!fmt) {
		fmt = &g_out_fmts[0];
		v4l2_err(v4l2_dev,
			"%s: set_fmt:%#x not support, use def_fmt:%x\n",
			__func__, pixm->pixelformat, fmt->fourcc);
	}

	if ((bt->width == 0) || (bt->height == 0))
		v4l2_err(v4l2_dev, "%s: err resolution:%#xx%#x!!!\n",
				__func__, bt->width, bt->height);

	pixm->width = bt->width;
	pixm->height = bt->height;
	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	pixm->quantization = V4L2_QUANTIZATION_DEFAULT;

	/* calculate plane size and image size */
	fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;

	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		int width, height, bpl, size, bpp;

		if (i == 0) {
			width = pixm->width;
			height = pixm->height;
		} else {
			width = pixm->width / xsubs;
			height = pixm->height / ysubs;
		}

		bpp = hdmirx_align_bits_per_pixel(fmt, i);
		bpl = ALIGN(width * bpp / HDMIRX_STORED_BIT_WIDTH,
				MEMORY_ALIGN_ROUND_UP_BYTES);
		size = bpl * height;
		imagesize += size;

		if (fmt->mplanes > i) {
			/* Set bpl and size for each mplane */
			plane_fmt = pixm->plane_fmt + i;
			plane_fmt->bytesperline = bpl;
			plane_fmt->sizeimage = size;
		}

		v4l2_dbg(1, debug, v4l2_dev,
			 "C-Plane %i size: %d, Total imagesize: %d\n",
			 i, size, imagesize);
	}

	/* convert to non-MPLANE format.
	 * It's important since we want to unify non-MPLANE and MPLANE.
	 */
	if (fmt->mplanes == 1)
		pixm->plane_fmt[0].sizeimage = imagesize;

	if (!try) {
		stream->out_fmt = fmt;
		stream->pixm = *pixm;

		v4l2_dbg(1, debug, v4l2_dev,
			"%s: req(%d, %d), out(%d, %d), fmt:%#x\n", __func__,
			pixm->width, pixm->height, stream->pixm.width,
			stream->pixm.height, fmt->fourcc);
	}
}

static int hdmirx_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->pixelformat != hdmirx_dev->cur_fmt_fourcc)
		return -EINVAL;

	hdmirx_set_fmt(stream, &f->fmt.pix_mp, true);

	return 0;
}

static int hdmirx_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_fmtdesc *f)
{
	const struct hdmirx_output_fmt *fmt;

	if (f->index >= ARRAY_SIZE(g_out_fmts))
		return -EINVAL;

	fmt = &g_out_fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int hdmirx_s_fmt_vid_cap_mplane(struct file *file,
				      void *priv, struct v4l2_format *f)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (vb2_is_busy(&stream->buf_queue)) {
		v4l2_err(v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	if (pix->pixelformat != hdmirx_dev->cur_fmt_fourcc) {
		v4l2_err(v4l2_dev, "%s: err, set_fmt:%#x, cur_fmt:%#x!\n",
			__func__, pix->pixelformat, hdmirx_dev->cur_fmt_fourcc);
		return -EINVAL;
	}

	hdmirx_set_fmt(stream, &f->fmt.pix_mp, false);

	return 0;
}

static int hdmirx_g_fmt_vid_cap_mplane(struct file *file, void *fh,
				      struct v4l2_format *f)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_pix_format_mplane pixm;

	pixm.pixelformat = hdmirx_dev->cur_fmt_fourcc;
	hdmirx_set_fmt(stream, &pixm, false);
	f->fmt.pix_mp = stream->pixm;

	return 0;
}

static int hdmirx_querycap(struct file *file, void *priv,
			  struct v4l2_capability *cap)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct device *dev = stream->hdmirx_dev->dev;

	strscpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strscpy(cap->card, dev->driver->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "%s", dev_name(dev));

	return 0;
}

static int hdmirx_queue_setup(struct vb2_queue *queue,
			     unsigned int *num_buffers,
			     unsigned int *num_planes,
			     unsigned int sizes[],
			     struct device *alloc_ctxs[])
{
	struct hdmirx_stream *stream = vb2_get_drv_priv(queue);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct hdmirx_output_fmt *out_fmt;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	u32 i, height;

	pixm = &stream->pixm;
	out_fmt = stream->out_fmt;

	if ((num_planes == NULL) || (out_fmt == NULL)) {
		v4l2_err(v4l2_dev, "%s: out_fmt null pointer err!\n", __func__);
		return -EINVAL;
	}
	*num_planes = out_fmt->mplanes;
	height = pixm->height;

	for (i = 0; i < out_fmt->mplanes; i++) {
		const struct v4l2_plane_pix_format *plane_fmt;
		int h = height;

		plane_fmt = &pixm->plane_fmt[i];
		sizes[i] = plane_fmt->sizeimage / height * h;
	}

	v4l2_dbg(1, debug, v4l2_dev, "%s count %d, size %d\n",
			v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
}

/*
 * The vb2_buffer are stored in hdmirx_buffer, in order to unify
 * mplane buffer and none-mplane buffer.
 */
static void hdmirx_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf;
	struct hdmirx_buffer *hdmirx_buf;
	struct vb2_queue *queue;
	struct hdmirx_stream *stream;
	struct v4l2_pix_format_mplane *pixm;
	const struct hdmirx_output_fmt *out_fmt;
	unsigned long lock_flags = 0;
	int i;

	if (vb == NULL) {
		pr_err("%s: vb null pointer err!\n", __func__);
		return;
	}

	vbuf = to_vb2_v4l2_buffer(vb);
	hdmirx_buf = to_hdmirx_buffer(vbuf);
	queue = vb->vb2_queue;
	stream = vb2_get_drv_priv(queue);
	pixm = &stream->pixm;
	out_fmt = stream->out_fmt;

	memset(hdmirx_buf->buff_addr, 0, sizeof(hdmirx_buf->buff_addr));
	/*
	 * If mplanes > 1, every c-plane has its own m-plane,
	 * otherwise, multiple c-planes are in the same m-plane
	 */
	for (i = 0; i < out_fmt->mplanes; i++)
		hdmirx_buf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

	if (out_fmt->mplanes == 1) {
		if (out_fmt->cplanes == 1) {
			hdmirx_buf->buff_addr[HDMIRX_PLANE_CBCR] =
				hdmirx_buf->buff_addr[HDMIRX_PLANE_Y];
		} else {
			for (i = 0; i < out_fmt->cplanes - 1; i++)
				hdmirx_buf->buff_addr[i + 1] =
					hdmirx_buf->buff_addr[i] +
					pixm->plane_fmt[i].bytesperline *
					pixm->height;
		}
	}

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	list_add_tail(&hdmirx_buf->queue, &stream->buf_head);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static void return_all_buffers(struct hdmirx_stream *stream,
			       enum vb2_buffer_state state)
{
	struct hdmirx_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (stream->curr_buf)
		list_add_tail(&stream->curr_buf->queue, &stream->buf_head);
	if ((stream->next_buf) && (stream->next_buf != stream->curr_buf))
		list_add_tail(&stream->next_buf->queue, &stream->buf_head);
	stream->curr_buf = NULL;
	stream->next_buf = NULL;

	while (!list_empty(&stream->buf_head)) {
		buf = list_first_entry(&stream->buf_head,
				       struct hdmirx_buffer, queue);
		list_del(&buf->queue);
		spin_unlock_irqrestore(&stream->vbq_lock, flags);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		spin_lock_irqsave(&stream->vbq_lock, flags);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
}

static void hdmirx_stop_streaming(struct vb2_queue *queue)
{
	struct hdmirx_stream *stream = vb2_get_drv_priv(queue);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	int ret;

	v4l2_info(v4l2_dev, "stream start stopping\n");
	mutex_lock(&hdmirx_dev->stream_lock);
	stream->stopping = true;

	/* wait last irq to return the buffer */
	ret = wait_event_timeout(stream->wq_stopped, stream->stopping != true,
			msecs_to_jiffies(500));
	if (!ret) {
		v4l2_err(v4l2_dev, "%s wait last irq timeout, return bufs!\n",
				__func__);
		stream->stopping = false;
	}

	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6, HDMIRX_DMA_EN, 0);
	return_all_buffers(stream, VB2_BUF_STATE_ERROR);
	mutex_unlock(&hdmirx_dev->stream_lock);
	v4l2_info(v4l2_dev, "stream stopping finished\n");
}

static int hdmirx_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct hdmirx_stream *stream = vb2_get_drv_priv(queue);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	unsigned long lock_flags = 0;
	struct v4l2_dv_timings timings = hdmirx_dev->timings;
	struct v4l2_bt_timings *bt = &timings.bt;

	mutex_lock(&hdmirx_dev->stream_lock);
	stream->frame_idx = 0;
	stream->dma_idle_cnt = 0;
	stream->curr_buf = NULL;
	stream->next_buf = NULL;
	stream->irq_stat = 0;
	stream->stopping = false;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (!stream->curr_buf) {
		if (!list_empty(&stream->buf_head)) {
			stream->curr_buf = list_first_entry(&stream->buf_head,
					struct hdmirx_buffer, queue);
			list_del(&stream->curr_buf->queue);
		} else {
			stream->curr_buf = NULL;
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	if (!stream->curr_buf) {
		mutex_unlock(&hdmirx_dev->stream_lock);
		return -ENOMEM;
	}

	v4l2_dbg(2, debug, v4l2_dev,
			"%s: start_stream cur_buf y_addr:%#x, uv_addr:%#x\n",
			__func__, stream->curr_buf->buff_addr[HDMIRX_PLANE_Y],
			stream->curr_buf->buff_addr[HDMIRX_PLANE_CBCR]);
	hdmirx_writel(hdmirx_dev, DMA_CONFIG2,
			stream->curr_buf->buff_addr[HDMIRX_PLANE_Y]);
	hdmirx_writel(hdmirx_dev, DMA_CONFIG3,
			stream->curr_buf->buff_addr[HDMIRX_PLANE_CBCR]);

	if (bt->height)
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG7,
				LINE_FLAG_NUM_MASK,
				LINE_FLAG_NUM(bt->height / 2));
	else
		v4l2_err(v4l2_dev, "height err: %d\n", bt->height);

	hdmirx_writel(hdmirx_dev, DMA_CONFIG5, 0xffffffff);
	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG4,
			LINE_FLAG_INT_EN |
			HDMIRX_DMA_IDLE_INT |
			HDMIRX_LOCK_DISABLE_INT |
			LAST_FRAME_AXI_UNFINISH_INT_EN |
			FIFO_OVERFLOW_INT_EN |
			FIFO_UNDERFLOW_INT_EN |
			HDMIRX_AXI_ERROR_INT_EN,
			LINE_FLAG_INT_EN |
			HDMIRX_DMA_IDLE_INT |
			HDMIRX_LOCK_DISABLE_INT |
			LAST_FRAME_AXI_UNFINISH_INT_EN |
			FIFO_OVERFLOW_INT_EN |
			FIFO_UNDERFLOW_INT_EN |
			HDMIRX_AXI_ERROR_INT_EN);
	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6, HDMIRX_DMA_EN, HDMIRX_DMA_EN);
	v4l2_dbg(1, debug, v4l2_dev, "%s: enable dma", __func__);
	mutex_unlock(&hdmirx_dev->stream_lock);

	return 0;
}

// ---------------------- vb2 queue -------------------------
static struct vb2_ops hdmirx_vb2_ops = {
	.queue_setup = hdmirx_queue_setup,
	.buf_queue = hdmirx_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = hdmirx_stop_streaming,
	.start_streaming = hdmirx_start_streaming,
};

static int hdmirx_init_vb2_queue(struct vb2_queue *q,
				struct hdmirx_stream *stream,
				enum v4l2_buf_type buf_type)
{
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;

	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = stream;
	q->ops = &hdmirx_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct hdmirx_buffer);
	q->min_buffers_needed = HDMIRX_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->vlock;
	q->dev = hdmirx_dev->dev;
	q->allow_cache_hints = 1;
	q->bidirectional = 1;
	q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	q->gfp_flags = GFP_DMA32;
	return vb2_queue_init(q);
}

// ---------------------- video device -------------------------
static const struct v4l2_ioctl_ops hdmirx_v4l2_ioctl_ops = {
	.vidioc_querycap = hdmirx_querycap,
	.vidioc_try_fmt_vid_cap_mplane = hdmirx_try_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = hdmirx_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = hdmirx_g_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap = hdmirx_enum_fmt_vid_cap_mplane,

	.vidioc_s_dv_timings = hdmirx_s_dv_timings,
	.vidioc_g_dv_timings = hdmirx_g_dv_timings,
	.vidioc_enum_dv_timings = hdmirx_enum_dv_timings,
	.vidioc_query_dv_timings = hdmirx_query_dv_timings,
	.vidioc_dv_timings_cap = hdmirx_dv_timings_cap,
	.vidioc_enum_input = hdmirx_enum_input,
	.vidioc_g_edid = hdmirx_get_edid,
	.vidioc_s_edid = hdmirx_set_edid,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,

	.vidioc_log_status = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = hdmirx_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations hdmirx_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int hdmirx_register_stream_vdev(struct hdmirx_stream *stream)
{
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct video_device *vdev = &stream->vdev;
	int ret = 0;
	char *vdev_name;

	vdev_name = HDMIRX_VDEV_NAME;
	strscpy(vdev->name, vdev_name, sizeof(vdev->name));
	INIT_LIST_HEAD(&stream->buf_head);
	spin_lock_init(&stream->vbq_lock);
	mutex_init(&stream->vlock);
	init_waitqueue_head(&stream->wq_stopped);
	stream->curr_buf = NULL;
	stream->next_buf = NULL;

	vdev->ioctl_ops = &hdmirx_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &hdmirx_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &stream->vlock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			    V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;

	hdmirx_init_vb2_queue(&stream->buf_queue, stream,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	vdev->queue = &stream->buf_queue;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "video_register_device failed with error %d\n", ret);
		return ret;
	}

	return 0;
}

static void process_signal_change(struct rk_hdmirx_dev *hdmirx_dev)
{
	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6, HDMIRX_DMA_EN, 0);
	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG4,
			LINE_FLAG_INT_EN |
			HDMIRX_DMA_IDLE_INT |
			HDMIRX_LOCK_DISABLE_INT |
			LAST_FRAME_AXI_UNFINISH_INT_EN |
			FIFO_OVERFLOW_INT_EN |
			FIFO_UNDERFLOW_INT_EN |
			HDMIRX_AXI_ERROR_INT_EN, 0);
	schedule_delayed_work(&hdmirx_dev->delayed_work_res_change,
			msecs_to_jiffies(10));
}

static void mainunit_0_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
		int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	v4l2_dbg(2, debug, v4l2_dev, "%s mu0_st:%#x\n", __func__, status);
	if (status & TIMER_BASE_LOCKED_IRQ) {
		hdmirx_update_bits(hdmirx_dev, MAINUNIT_0_INT_MASK_N,
				   TIMER_BASE_LOCKED_IRQ, 0);
		hdmirx_dev->timer_base_lock = true;
		*handled = true;
	}

	if (status & TMDSQPCLK_OFF_CHG) {
		process_signal_change(hdmirx_dev);
		v4l2_dbg(2, debug, v4l2_dev, "%s: TMDSQPCLK_OFF_CHG\n", __func__);
		*handled = true;
	}

	if (status & TMDSQPCLK_LOCKED_CHG) {
		process_signal_change(hdmirx_dev);
		v4l2_dbg(2, debug, v4l2_dev, "%s: TMDSQPCLK_LOCKED_CHG\n", __func__);
		*handled = true;
	}

	hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_CLEAR, 0xffffffff);
}

static void mainunit_2_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
		int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	v4l2_dbg(2, debug, v4l2_dev, "%s mu2_st:%#x\n", __func__, status);
	if (status & PHYCREG_CR_WRITE_DONE) {
		hdmirx_update_bits(hdmirx_dev, MAINUNIT_2_INT_MASK_N,
				   PHYCREG_CR_WRITE_DONE, 0);
		hdmirx_dev->cr_write_done = true;
		*handled = true;
	}

	if (status & PHYCREG_CR_READ_DONE) {
		hdmirx_update_bits(hdmirx_dev, MAINUNIT_2_INT_MASK_N,
				   PHYCREG_CR_READ_DONE, 0);
		hdmirx_dev->cr_read_done = true;
		*handled = true;
	}

	if (status & TMDSVALID_STABLE_CHG) {
		process_signal_change(hdmirx_dev);
		schedule_delayed_work(&hdmirx_dev->delayed_work_res_change,
				msecs_to_jiffies(10));
		v4l2_dbg(2, debug, v4l2_dev, "%s: TMDSVALID_STABLE_CHG\n", __func__);
		*handled = true;
	}

	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_CLEAR, 0xffffffff);
}

static void pkt_2_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
		int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	v4l2_dbg(2, debug, v4l2_dev, "%s pk2_st:%#x\n", __func__, status);
	if (status & PKTDEC_AVIIF_RCV_IRQ) {
		hdmirx_update_bits(hdmirx_dev, PKT_2_INT_MASK_N,
				PKTDEC_AVIIF_RCV_IRQ, 0);
		hdmirx_dev->avi_pkt_rcv = true;
		v4l2_dbg(2, debug, v4l2_dev, "%s: AVIIF_RCV_IRQ\n", __func__);
		*handled = true;
	}

	hdmirx_writel(hdmirx_dev, PKT_2_INT_CLEAR, 0xffffffff);
}

static void scdc_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
		int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	u32 val;

	v4l2_dbg(2, debug, v4l2_dev, "%s scdc_st:%#x\n", __func__, status);
	if (status & SCDCTMDSCCFG_CHG) {
		val = hdmirx_readl(hdmirx_dev, SCDC_REGBANK_STATUS1);
		v4l2_dbg(2, debug, v4l2_dev, "%s scdc_regbank_st:%#x\n",
				__func__, val);
		hdmirx_dev->tmds_clk_ratio = (val  & SCDC_TMDSBITCLKRATIO) > 0;
		if (hdmirx_dev->tmds_clk_ratio) {
			v4l2_dbg(1, debug, v4l2_dev, "%s HDMITX greater than 3.4Gbps!\n", __func__);
			hdmirx_update_bits(hdmirx_dev, PHY_CONFIG,
					   TMDS_CLOCK_RATIO, TMDS_CLOCK_RATIO);
		} else {
			v4l2_dbg(1, debug, v4l2_dev, "%s HDMITX less than 3.4Gbps!\n", __func__);
			hdmirx_update_bits(hdmirx_dev, PHY_CONFIG,
					   TMDS_CLOCK_RATIO, 0);
		}
		*handled = true;
	}

	hdmirx_writel(hdmirx_dev, SCDC_INT_CLEAR, 0xffffffff);
}

static irqreturn_t hdmirx_hdmi_irq_handler(int irq, void *dev_id)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_id;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	bool handled = false;
	u32 mu0_st, mu2_st, pk2_st, scdc_st;
	u32 mu0_mask, mu2_mask, pk2_mask, scdc_mask;

	mu0_mask = hdmirx_readl(hdmirx_dev, MAINUNIT_0_INT_MASK_N);
	mu2_mask = hdmirx_readl(hdmirx_dev, MAINUNIT_2_INT_MASK_N);
	pk2_mask = hdmirx_readl(hdmirx_dev, PKT_2_INT_MASK_N);
	scdc_mask = hdmirx_readl(hdmirx_dev, SCDC_INT_MASK_N);
	mu0_st = hdmirx_readl(hdmirx_dev, MAINUNIT_0_INT_STATUS);
	mu2_st = hdmirx_readl(hdmirx_dev, MAINUNIT_2_INT_STATUS);
	pk2_st = hdmirx_readl(hdmirx_dev, PKT_2_INT_STATUS);
	scdc_st = hdmirx_readl(hdmirx_dev, SCDC_INT_STATUS);
	mu0_st &= mu0_mask;
	mu2_st &= mu2_mask;
	pk2_st &= pk2_mask;
	scdc_st &= scdc_mask;

	if (mu0_st)
		mainunit_0_int_handler(hdmirx_dev, mu0_st, &handled);
	if (mu2_st)
		mainunit_2_int_handler(hdmirx_dev, mu2_st, &handled);
	if (pk2_st)
		pkt_2_int_handler(hdmirx_dev, pk2_st, &handled);
	if (scdc_st)
		scdc_int_handler(hdmirx_dev, scdc_st, &handled);

	if (!handled)
		v4l2_dbg(2, debug, v4l2_dev, "%s: hdmi irq not handled!\n",
				__func__);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static void hdmirx_vb_done(struct hdmirx_stream *stream,
				   struct vb2_v4l2_buffer *vb_done)
{
	const struct hdmirx_output_fmt *fmt = stream->out_fmt;
	u32 i;

	/* Dequeue a filled buffer */
	for (i = 0; i < fmt->mplanes; i++) {
		vb2_set_plane_payload(&vb_done->vb2_buf, i,
				      stream->pixm.plane_fmt[i].sizeimage);
	}

	vb_done->vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&vb_done->vb2_buf, VB2_BUF_STATE_DONE);
}

static void dma_idle_int_handler(struct rk_hdmirx_dev *hdmirx_dev, bool *handled)
{
	struct hdmirx_stream *stream = &hdmirx_dev->stream;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct v4l2_dv_timings timings = hdmirx_dev->timings;
	struct v4l2_bt_timings *bt = &timings.bt;
	struct vb2_v4l2_buffer *vb_done = NULL;

	stream->dma_idle_cnt++;
	if (!(stream->irq_stat) && !(stream->irq_stat & LINE_FLAG_INT_EN))
		v4l2_dbg(1, debug, v4l2_dev,
			 "%s: last time have no line_flag_irq\n", __func__);

	if ((bt->interlaced != V4L2_DV_INTERLACED) ||
			(stream->dma_idle_cnt % 2 == 0)) {
		if (stream->next_buf) {
			if (stream->curr_buf)
				vb_done = &stream->curr_buf->vb;

			if (vb_done) {
				vb_done->vb2_buf.timestamp = ktime_get_ns();
				vb_done->sequence = stream->frame_idx;
				hdmirx_vb_done(stream, vb_done);
				stream->frame_idx++;
			}

			stream->curr_buf = NULL;
			if (stream->next_buf) {
				stream->curr_buf = stream->next_buf;
				stream->next_buf = NULL;
			}
		} else {
			v4l2_dbg(3, debug, v4l2_dev,
				 "%s: next_buf NULL, skip vb_done!\n", __func__);
		}
	}

	*handled = true;
}

static void line_flag_int_handler(struct rk_hdmirx_dev *hdmirx_dev, bool *handled)
{
	struct hdmirx_stream *stream = &hdmirx_dev->stream;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct v4l2_dv_timings timings = hdmirx_dev->timings;
	struct v4l2_bt_timings *bt = &timings.bt;
	u32 dma_cfg6;

	if (!(stream->irq_stat) && !(stream->irq_stat & HDMIRX_DMA_IDLE_INT))
		v4l2_dbg(1, debug, v4l2_dev,
			 "%s: last have no dma_idle_irq\n", __func__);
	dma_cfg6 = hdmirx_readl(hdmirx_dev, DMA_CONFIG6);
	if (!(dma_cfg6 & HDMIRX_DMA_EN)) {
		v4l2_dbg(2, debug, v4l2_dev, "%s: dma not on\n", __func__);
		goto LINE_FLAG_OUT;
	}

	if ((bt->interlaced != V4L2_DV_INTERLACED) ||
			(stream->dma_idle_cnt % 2 == 0)) {
		if (!stream->next_buf) {
			spin_lock(&stream->vbq_lock);
			if (!list_empty(&stream->buf_head)) {
				stream->next_buf = list_first_entry(&stream->buf_head,
						struct hdmirx_buffer, queue);
				list_del(&stream->next_buf->queue);
			} else {
				stream->next_buf = NULL;
			}
			spin_unlock(&stream->vbq_lock);

			if (stream->next_buf) {
				hdmirx_writel(hdmirx_dev, DMA_CONFIG2,
					stream->next_buf->buff_addr[HDMIRX_PLANE_Y]);
				hdmirx_writel(hdmirx_dev, DMA_CONFIG3,
					stream->next_buf->buff_addr[HDMIRX_PLANE_CBCR]);
			} else {
				v4l2_dbg(3, debug, v4l2_dev,
					 "%s: No buffer is available\n", __func__);
			}
		}
	} else {
		v4l2_dbg(3, debug, v4l2_dev, "%s: interlace:%d, dma_idle_cnt:%d\n",
			 __func__, bt->interlaced, stream->dma_idle_cnt);
	}

LINE_FLAG_OUT:
	*handled = true;
}

static irqreturn_t hdmirx_dma_irq_handler(int irq, void *dev_id)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_id;
	struct hdmirx_stream *stream = &hdmirx_dev->stream;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	u32 dma_stat1, dma_stat13;
	bool handled = false;

	dma_stat1 = hdmirx_readl(hdmirx_dev, DMA_STATUS1);
	dma_stat13 = hdmirx_readl(hdmirx_dev, DMA_STATUS13);
	v4l2_dbg(3, debug, v4l2_dev, "dma_irq st1:%#x, st13:%d\n",
			dma_stat1, dma_stat13);

	if (stream->stopping) {
		v4l2_dbg(1, debug, v4l2_dev, "%s: stop stream!\n", __func__);
		stream->stopping = false;
		hdmirx_writel(hdmirx_dev, DMA_CONFIG5, 0xffffffff);
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG4,
				LINE_FLAG_INT_EN |
				HDMIRX_DMA_IDLE_INT |
				HDMIRX_LOCK_DISABLE_INT |
				LAST_FRAME_AXI_UNFINISH_INT_EN |
				FIFO_OVERFLOW_INT_EN |
				FIFO_UNDERFLOW_INT_EN |
				HDMIRX_AXI_ERROR_INT_EN, 0);
		wake_up(&stream->wq_stopped);
		return IRQ_HANDLED;
	}

	if (dma_stat1 & HDMIRX_DMA_IDLE_INT)
		dma_idle_int_handler(hdmirx_dev, &handled);

	if (dma_stat1 & LINE_FLAG_INT_EN)
		line_flag_int_handler(hdmirx_dev, &handled);

	if (!handled)
		v4l2_dbg(2, debug, v4l2_dev,
			 "%s: dma irq not handled, dma_stat1:%#x!\n",
			 __func__, dma_stat1);

	stream->irq_stat = dma_stat1;
	hdmirx_writel(hdmirx_dev, DMA_CONFIG5, 0xffffffff);

	return IRQ_HANDLED;
}

static void hdmirx_interrupts_setup(struct rk_hdmirx_dev *hdmirx_dev, bool en)
{
	v4l2_dbg(1, debug, &hdmirx_dev->v4l2_dev, "%s: %sable\n",
			__func__, en ? "en" : "dis");

	/* Note: In DVI mode, it needs to be written twice to take effect. */
	hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_CLEAR, 0xffffffff);

	if (en) {
		hdmirx_update_bits(hdmirx_dev, MAINUNIT_0_INT_MASK_N,
				TMDSQPCLK_OFF_CHG | TMDSQPCLK_LOCKED_CHG,
				TMDSQPCLK_OFF_CHG | TMDSQPCLK_LOCKED_CHG);
		hdmirx_update_bits(hdmirx_dev, MAINUNIT_2_INT_MASK_N,
				TMDSVALID_STABLE_CHG, TMDSVALID_STABLE_CHG);
	} else {
		hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_MASK_N, 0);
		hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_MASK_N, 0);
	}
}

static void hdmirx_delayed_work_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk_hdmirx_dev *hdmirx_dev = container_of(dwork,
			struct rk_hdmirx_dev, delayed_work_hotplug);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	bool plugin;

	mutex_lock(&hdmirx_dev->work_lock);
	plugin = tx_5v_power_present(hdmirx_dev);
	v4l2_ctrl_s_ctrl(hdmirx_dev->detect_tx_5v_ctrl, plugin);
	v4l2_dbg(1, debug, v4l2_dev, "%s: plugin:%d\n", __func__, plugin);

	if (plugin) {
		hdmirx_update_bits(hdmirx_dev, SCDC_CONFIG, POWERPROVIDED,
					POWERPROVIDED);
		hdmirx_hpd_ctrl(hdmirx_dev, true);
		hdmirx_phy_config(hdmirx_dev);
		hdmirx_wait_lock_and_get_timing(hdmirx_dev);
		hdmirx_dma_config(hdmirx_dev);
		hdmirx_interrupts_setup(hdmirx_dev, true);
	} else {
		hdmirx_update_bits(hdmirx_dev, SCDC_CONFIG, POWERPROVIDED, 0);
		hdmirx_interrupts_setup(hdmirx_dev, false);
		hdmirx_hpd_ctrl(hdmirx_dev, false);
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6, HDMIRX_DMA_EN, 0);
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG4,
				LINE_FLAG_INT_EN |
				HDMIRX_DMA_IDLE_INT |
				HDMIRX_LOCK_DISABLE_INT |
				LAST_FRAME_AXI_UNFINISH_INT_EN |
				FIFO_OVERFLOW_INT_EN |
				FIFO_UNDERFLOW_INT_EN |
				HDMIRX_AXI_ERROR_INT_EN, 0);
		cancel_delayed_work(&hdmirx_dev->delayed_work_res_change);
	}
	mutex_unlock(&hdmirx_dev->work_lock);
}

static void hdmirx_delayed_work_res_change(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk_hdmirx_dev *hdmirx_dev = container_of(dwork,
			struct rk_hdmirx_dev, delayed_work_res_change);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	bool plugin;

	mutex_lock(&hdmirx_dev->work_lock);
	plugin = tx_5v_power_present(hdmirx_dev);
	v4l2_dbg(1, debug, v4l2_dev, "%s: plugin:%d\n", __func__, plugin);
	if (plugin) {
		hdmirx_interrupts_setup(hdmirx_dev, false);
		hdmirx_wait_lock_and_get_timing(hdmirx_dev);
		hdmirx_dma_config(hdmirx_dev);
		hdmirx_interrupts_setup(hdmirx_dev, true);
	}
	mutex_unlock(&hdmirx_dev->work_lock);
}

static irqreturn_t hdmirx_5v_det_irq_handler(int irq, void *dev_id)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_id;

	schedule_delayed_work(&hdmirx_dev->delayed_work_hotplug,
				msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static int hdmirx_parse_dt(struct rk_hdmirx_dev *hdmirx_dev)
{
	struct device *dev = hdmirx_dev->dev;
	struct device_node *np = hdmirx_dev->of_node;
	int ret;

	if (!np) {
		dev_err(dev, "missing DT node\n");
		return -EINVAL;
	}

	hdmirx_dev->num_clks = devm_clk_bulk_get_all(dev, &hdmirx_dev->clks);
	if (hdmirx_dev->num_clks < 1)
		return -ENODEV;

	hdmirx_dev->reset = devm_reset_control_array_get(dev, false, false);
	if (IS_ERR(hdmirx_dev->reset)) {
		if (PTR_ERR(hdmirx_dev->reset) != -EPROBE_DEFER)
			dev_err(dev, "failed to get hdmirx reset lines\n");
		return PTR_ERR(hdmirx_dev->reset);
	}

	hdmirx_dev->hdmirx_det_gpio = devm_gpiod_get_optional(dev,
			"hdmirx-det", GPIOD_IN);
	if (IS_ERR(hdmirx_dev->hdmirx_det_gpio)) {
		dev_err(dev, "failed to get hdmirx det gpio\n");
		return PTR_ERR(hdmirx_dev->hdmirx_det_gpio);
	}

	hdmirx_dev->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmirx_dev->grf)) {
		dev_err(dev, "failed to get rockchip,grf\n");
		return PTR_ERR(hdmirx_dev->grf);
	}

	hdmirx_dev->vo1_grf = syscon_regmap_lookup_by_phandle(np,
			"rockchip,vo1_grf");
	if (IS_ERR(hdmirx_dev->vo1_grf)) {
		dev_err(dev, "failed to get rockchip,vo1_grf\n");
		return PTR_ERR(hdmirx_dev->vo1_grf);
	}

	if (of_property_read_u32(np, "hpd-trigger-level",
					&hdmirx_dev->hpd_trigger_level)) {
		hdmirx_dev->hpd_trigger_level = 1;
		dev_warn(dev, "failed to get hpd-trigger-level, set high as default\n");
	}

	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		dev_err(dev, "No reserved memory for HDMIRX, ret:%d\n", ret);
		return -ENOMEM;
	}

	return 0;
}

static void hdmirx_disable_all_interrupts(struct rk_hdmirx_dev *hdmirx_dev)
{
	hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, MAINUNIT_1_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, AVPUNIT_0_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, AVPUNIT_1_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, PKT_0_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, PKT_1_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, PKT_2_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, SCDC_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, HDCP_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, HDCP_1_INT_MASK_N, 0);
	hdmirx_writel(hdmirx_dev, CEC_INT_MASK_N, 0);

	hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, MAINUNIT_1_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, AVPUNIT_0_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, AVPUNIT_1_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, PKT_0_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, PKT_1_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, PKT_2_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, SCDC_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, HDCP_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, HDCP_1_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, CEC_INT_CLEAR, 0xffffffff);
}

static int hdmirx_power_on(struct rk_hdmirx_dev *hdmirx_dev)
{
	pm_runtime_enable(hdmirx_dev->dev);
	pm_runtime_get_sync(hdmirx_dev->dev);
	hdmirx_dev->power_on = true;

	regmap_write(hdmirx_dev->vo1_grf, VO1_GRF_VO1_CON2,
		(HDCP1_GATING_EN | HDMIRX_SDAIN_MSK | HDMIRX_SCLIN_MSK) |
		((HDCP1_GATING_EN | HDMIRX_SDAIN_MSK | HDMIRX_SCLIN_MSK) << 16));

	/*
	 * Some interrupts are enabled by default, so we disable
	 * all interrupts and clear interrupts status first.
	 */
	hdmirx_disable_all_interrupts(hdmirx_dev);

	return 0;
}

static void hdmirx_edid_init_config(struct rk_hdmirx_dev *hdmirx_dev)
{
	int ret;
	struct v4l2_edid def_edid;

	/* disable hpd and write edid */
	def_edid.pad = 0;
	def_edid.start_block = 0;
	def_edid.blocks = EDID_NUM_BLOCKS_MAX;
	def_edid.edid = edid_init_data;
	ret = hdmirx_write_edid(hdmirx_dev, &def_edid, false);
	if (ret)
		dev_err(hdmirx_dev->dev, "%s write edid failed!\n", __func__);
}

static int hdmirx_runtime_suspend(struct device *dev)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	hdmirx_disable_all_interrupts(hdmirx_dev);
	disable_irq(hdmirx_dev->hdmi_irq);
	disable_irq(hdmirx_dev->dma_irq);
	v4l2_dbg(2, debug, v4l2_dev, "%s: suspend!\n", __func__);

	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, PHY_RESET, PHY_RESET);
	usleep_range(100, 110);
	reset_control_assert(hdmirx_dev->reset);
	usleep_range(100, 110);
	clk_bulk_disable_unprepare(hdmirx_dev->num_clks, hdmirx_dev->clks);

	return pinctrl_pm_select_sleep_state(dev);
}

static int hdmirx_runtime_resume(struct device *dev)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	int ret;

	v4l2_dbg(2, debug, v4l2_dev, "%s: resume!\n", __func__);
	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;

	ret = clk_bulk_prepare_enable(hdmirx_dev->num_clks, hdmirx_dev->clks);
	if (ret) {
		dev_err(dev, "failed to enable hdmirx bulk clks: %d\n", ret);
		return ret;
	}
	enable_irq(hdmirx_dev->hdmi_irq);
	enable_irq(hdmirx_dev->dma_irq);

	reset_control_assert(hdmirx_dev->reset);
	usleep_range(150, 160);
	reset_control_deassert(hdmirx_dev->reset);
	usleep_range(150, 160);

	return 0;
}

static const struct dev_pm_ops rk_hdmirx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(hdmirx_runtime_suspend, hdmirx_runtime_resume, NULL)
};

static int hdmirx_probe(struct platform_device *pdev)
{
	const struct v4l2_dv_timings timings_def = HDMIRX_DEFAULT_TIMING;
	struct device *dev = &pdev->dev;
	struct rk_hdmirx_dev *hdmirx_dev;
	struct hdmirx_stream *stream;
	struct v4l2_device *v4l2_dev;
	struct v4l2_ctrl_handler *hdl;
	struct resource *res;
	int ret, irq;

	hdmirx_dev = devm_kzalloc(dev, sizeof(*hdmirx_dev), GFP_KERNEL);
	if (!hdmirx_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, hdmirx_dev);
	hdmirx_dev->dev = dev;
	hdmirx_dev->of_node = dev->of_node;
	ret = hdmirx_parse_dt(hdmirx_dev);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdmirx_regs");
	hdmirx_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdmirx_dev->regs)) {
		dev_err(dev, "failed to remap regs resource\n");
		return PTR_ERR(hdmirx_dev->regs);
	}

	mutex_init(&hdmirx_dev->stream_lock);
	mutex_init(&hdmirx_dev->work_lock);
	INIT_DELAYED_WORK(&hdmirx_dev->delayed_work_hotplug,
			hdmirx_delayed_work_hotplug);
	INIT_DELAYED_WORK(&hdmirx_dev->delayed_work_res_change,
			hdmirx_delayed_work_res_change);
	hdmirx_dev->power_on = false;

	ret = hdmirx_power_on(hdmirx_dev);
	if (ret)
		goto err_work_queues;

	hdmirx_edid_init_config(hdmirx_dev);
	hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_RGB24;
	hdmirx_dev->timings = timings_def;

	irq = platform_get_irq_byname(pdev, "hdmi");
	if (irq < 0) {
		dev_err(dev, "get hdmi irq failed!\n");
		ret = irq;
		goto err_work_queues;
	}

	hdmirx_dev->hdmi_irq = irq;
	ret = devm_request_threaded_irq(dev, irq, NULL, hdmirx_hdmi_irq_handler,
			IRQF_ONESHOT, RK_HDMIRX_DRVNAME"-hdmi", hdmirx_dev);
	if (ret) {
		dev_err(dev, "request hdmi irq thread failed!\n");
		goto err_work_queues;
	}

	irq = platform_get_irq_byname(pdev, "dma");
	if (irq < 0) {
		dev_err(dev, "get dma irq failed!\n");
		ret = irq;
		goto err_work_queues;
	}

	hdmirx_dev->dma_irq = irq;
	ret = devm_request_threaded_irq(dev, irq, NULL, hdmirx_dma_irq_handler,
			IRQF_ONESHOT, RK_HDMIRX_DRVNAME"-dma", hdmirx_dev);
	if (ret) {
		dev_err(dev, "request dma irq thread failed!\n");
		goto err_work_queues;
	}

	hdmirx_submodule_init(hdmirx_dev);

	v4l2_dev = &hdmirx_dev->v4l2_dev;
	strscpy(v4l2_dev->name, dev_name(dev), sizeof(v4l2_dev->name));

	hdl = &hdmirx_dev->hdl;
	v4l2_ctrl_handler_init(hdl, 1);
	hdmirx_dev->detect_tx_5v_ctrl = v4l2_ctrl_new_std(hdl,
			NULL, V4L2_CID_DV_RX_POWER_PRESENT,
			0, 1, 0, 0);
	if (hdl->error) {
		dev_err(dev, "v4l2 ctrl handler init failed!\n");
		ret = hdl->error;
		goto err_work_queues;
	}
	hdmirx_dev->v4l2_dev.ctrl_handler = hdl;

	ret = v4l2_device_register(dev, &hdmirx_dev->v4l2_dev);
	if (ret < 0) {
		dev_err(dev, "register v4l2 device failed!\n");
		goto err_hdl;
	}

	stream = &hdmirx_dev->stream;
	stream->hdmirx_dev = hdmirx_dev;
	ret = hdmirx_register_stream_vdev(stream);
	if (ret < 0) {
		dev_err(dev, "register video device failed!\n");
		goto err_unreg_v4l2_dev;
	}

	irq = gpiod_to_irq(hdmirx_dev->hdmirx_det_gpio);
	if (irq < 0) {
		dev_err(dev, "failed to get hdmirx-det gpio irq\n");
		ret = irq;
		goto err_unreg_video_dev;
	}

	hdmirx_dev->det_irq = irq;
	ret = devm_request_threaded_irq(dev, irq, NULL,
			hdmirx_5v_det_irq_handler, IRQF_ONESHOT |
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			RK_HDMIRX_DRVNAME"-5v", hdmirx_dev);
	if (ret) {
		dev_err(dev, "request hdmirx-det gpio irq thread failed!\n");
		goto err_unreg_video_dev;
	}

	schedule_delayed_work(&hdmirx_dev->delayed_work_hotplug,
			msecs_to_jiffies(2000));
	dev_info(dev, "%s driver probe ok!\n", dev_name(dev));

	return 0;

err_unreg_video_dev:
	video_unregister_device(&hdmirx_dev->stream.vdev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&hdmirx_dev->v4l2_dev);
err_hdl:
	v4l2_ctrl_handler_free(&hdmirx_dev->hdl);
err_work_queues:
	cancel_delayed_work(&hdmirx_dev->delayed_work_hotplug);
	cancel_delayed_work(&hdmirx_dev->delayed_work_res_change);
	clk_bulk_disable_unprepare(hdmirx_dev->num_clks, hdmirx_dev->clks);
	if (hdmirx_dev->power_on)
		pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int hdmirx_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);

	cancel_delayed_work(&hdmirx_dev->delayed_work_hotplug);
	cancel_delayed_work(&hdmirx_dev->delayed_work_res_change);
	clk_bulk_disable_unprepare(hdmirx_dev->num_clks, hdmirx_dev->clks);
	reset_control_assert(hdmirx_dev->reset);

	video_unregister_device(&hdmirx_dev->stream.vdev);
	v4l2_ctrl_handler_free(&hdmirx_dev->hdl);
	v4l2_device_unregister(&hdmirx_dev->v4l2_dev);

	if (hdmirx_dev->power_on)
		pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

static const struct of_device_id hdmirx_id[] = {
	{ .compatible = "rockchip,hdmirx-ctrler" },
	{ },
};
MODULE_DEVICE_TABLE(of, hdmirx_id);

static struct platform_driver hdmirx_driver = {
	.probe = hdmirx_probe,
	.remove = hdmirx_remove,
	.driver = {
		.name = RK_HDMIRX_DRVNAME,
		.of_match_table = hdmirx_id,
		.pm = &rk_hdmirx_pm_ops,
	}
};
module_platform_driver(hdmirx_driver);

MODULE_DESCRIPTION("Rockchip HDMI Receiver Driver");
MODULE_AUTHOR("Dingxian Wen <shawn.wen@rock-chips.com>");
MODULE_LICENSE("GPL v2");
