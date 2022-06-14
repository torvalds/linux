// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Dingxian Wen <shawn.wen@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
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
#include <linux/rockchip/rockchip_sip.h>
#include <linux/seq_file.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/workqueue.h>
#include <media/cec.h>
#include <media/cec-notifier.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <sound/hdmi-codec.h>
#include "rk_hdmirx.h"
#include "rk_hdmirx_cec.h"
#include "rk_hdmirx_hdcp.h"

static struct class *hdmirx_class;
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
#define INIT_FIFO_STATE			64
#define RK_IRQ_HDMIRX_HDMI		210
#define FILTER_FRAME_CNT		6
#define CPU_LIMIT_FREQ_KHZ		1200000

#define is_validfs(x) (x == 32000 || \
			x == 44100 || \
			x == 48000 || \
			x == 88200 || \
			x == 96000 || \
			x == 176400 || \
			x == 192000 || \
			x == 768000)

struct hdmirx_audiostate {
	struct platform_device *pdev;
	u32 hdmirx_aud_clkrate;
	u32 fs_audio;
	u32 ch_audio;
	u32 ctsn_flag;
	u32 fifo_flag;
	int init_state;
	int pre_state;
	bool fifo_int;
	bool audio_enabled;
};

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

enum hdmirx_reg_attr {
	HDMIRX_ATTR_RW = 0,
	HDMIRX_ATTR_RO = 1,
	HDMIRX_ATTR_WO = 2,
	HDMIRX_ATTR_RE = 3,
};

enum hdmirx_edid_version {
	HDMIRX_EDID_USER = 0,
	HDMIRX_EDID_340M = 1,
	HDMIRX_EDID_600M = 2,
};

struct hdmirx_reg_table {
	int reg_base;
	int reg_end;
	enum hdmirx_reg_attr attr;
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
	u32 line_flag_int_cnt;
	u32 irq_stat;
};

struct rk_hdmirx_dev {
	struct cec_notifier *cec_notifier;
	struct cpufreq_policy *policy;
	struct device *dev;
	struct device *classdev;
	struct device *codec_dev;
	struct device_node *of_node;
	struct hdmirx_stream stream;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_dv_timings timings;
	struct gpio_desc *hdmirx_det_gpio;
	struct work_struct work_wdt_config;
	struct delayed_work delayed_work_hotplug;
	struct delayed_work delayed_work_res_change;
	struct delayed_work delayed_work_audio;
	struct delayed_work delayed_work_heartbeat;
	struct dentry *debugfs_dir;
	struct freq_qos_request min_sta_freq_req;
	struct hdmirx_audiostate audio_state;
	struct hdmirx_cec *cec;
	struct mutex stream_lock;
	struct mutex work_lock;
	struct pm_qos_request pm_qos;
	struct reset_control *rst_a;
	struct reset_control *rst_p;
	struct reset_control *rst_ref;
	struct reset_control *rst_biu;
	struct clk_bulk_data *clks;
	struct regmap *grf;
	struct regmap *vo1_grf;
	struct rk_hdmirx_hdcp *hdcp;
	void __iomem *regs;
	int edid_version;
	int audio_present;
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
	bool initialized;
	bool freq_qos_add;
	bool hdcp1x_enable;
	bool get_timing;
	u32 num_clks;
	u32 edid_blocks_written;
	u32 hpd_trigger_level;
	u32 cur_vic;
	u32 cur_fmt_fourcc;
	u32 color_depth;
	u32 cpu_freq_khz;
	u32 bound_cpu;
	u32 wdt_cfg_bound_cpu;
	u8 edid[EDID_BLOCK_SIZE * 2];
	hdmi_codec_plugged_cb plugged_cb;
	spinlock_t dma_rst_lock;
};

static bool tx_5v_power_present(struct rk_hdmirx_dev *hdmirx_dev);
static void hdmirx_set_fmt(struct hdmirx_stream *stream,
		struct v4l2_pix_format_mplane *pixm, bool try);
static void hdmirx_audio_setup(struct rk_hdmirx_dev *hdmirx_dev);
static u32 hdmirx_audio_fs(struct rk_hdmirx_dev *hdmirx_dev);
static u32 hdmirx_audio_ch(struct rk_hdmirx_dev *hdmirx_dev);
static void hdmirx_audio_handle_plugged_change(struct rk_hdmirx_dev *hdmirx_dev, bool plugged);
static void hdmirx_audio_interrupts_setup(struct rk_hdmirx_dev *hdmirx_dev, bool en);
static int hdmirx_set_cpu_limit_freq(struct rk_hdmirx_dev *hdmirx_dev);
static void hdmirx_cancel_cpu_limit_freq(struct rk_hdmirx_dev *hdmirx_dev);
static void hdmirx_plugout(struct rk_hdmirx_dev *hdmirx_dev);

static u8 edid_init_data_340M[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x49, 0x70, 0x88, 0x35, 0x01, 0x00, 0x00, 0x00,
	0x2D, 0x1F, 0x01, 0x03, 0x80, 0x78, 0x44, 0x78,
	0x0A, 0xCF, 0x74, 0xA3, 0x57, 0x4C, 0xB0, 0x23,
	0x09, 0x48, 0x4C, 0x21, 0x08, 0x00, 0x61, 0x40,
	0x01, 0x01, 0x81, 0x00, 0x95, 0x00, 0xA9, 0xC0,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A,
	0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0x20, 0xC2, 0x31, 0x00, 0x00, 0x1E,
	0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
	0x6E, 0x28, 0x55, 0x00, 0x20, 0xC2, 0x31, 0x00,
	0x00, 0x1E, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x52,
	0x4B, 0x2D, 0x55, 0x48, 0x44, 0x0A, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFD,
	0x00, 0x3B, 0x46, 0x1F, 0x8C, 0x3C, 0x00, 0x0A,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xA7,

	0x02, 0x03, 0x2F, 0xD1, 0x51, 0x07, 0x16, 0x14,
	0x05, 0x01, 0x03, 0x12, 0x13, 0x84, 0x22, 0x1F,
	0x90, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x23, 0x09,
	0x07, 0x07, 0x83, 0x01, 0x00, 0x00, 0x67, 0x03,
	0x0C, 0x00, 0x30, 0x00, 0x10, 0x44, 0xE3, 0x05,
	0x03, 0x01, 0xE4, 0x0F, 0x00, 0x80, 0x01, 0x02,
	0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58,
	0x2C, 0x45, 0x00, 0x20, 0xC2, 0x31, 0x00, 0x00,
	0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
};

static u8 edid_init_data_600M[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x49, 0x70, 0x88, 0x35, 0x01, 0x00, 0x00, 0x00,
	0x2D, 0x1F, 0x01, 0x03, 0x80, 0x78, 0x44, 0x78,
	0x0A, 0xCF, 0x74, 0xA3, 0x57, 0x4C, 0xB0, 0x23,
	0x09, 0x48, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x08, 0xE8,
	0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80, 0xB0, 0x58,
	0x8A, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E,
	0x08, 0xE8, 0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80,
	0xB0, 0x58, 0x8A, 0x00, 0x20, 0xC2, 0x31, 0x00,
	0x00, 0x1E, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x52,
	0x4B, 0x2D, 0x55, 0x48, 0x44, 0x0A, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFD,
	0x00, 0x3B, 0x46, 0x1F, 0x8C, 0x3C, 0x00, 0x0A,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x39,

	0x02, 0x03, 0x21, 0xD2, 0x41, 0x61, 0x23, 0x09,
	0x07, 0x07, 0x83, 0x01, 0x00, 0x00, 0x66, 0x03,
	0x0C, 0x00, 0x30, 0x00, 0x10, 0x67, 0xD8, 0x5D,
	0xC4, 0x01, 0x78, 0xC0, 0x07, 0xE3, 0x05, 0x03,
	0x01, 0x08, 0xE8, 0x00, 0x30, 0xF2, 0x70, 0x5A,
	0x80, 0xB0, 0x58, 0x8A, 0x00, 0xC4, 0x8E, 0x21,
	0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE8,
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
		.fourcc = V4L2_PIX_FMT_BGR24,
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

static void hdmirx_writel(struct rk_hdmirx_dev *hdmirx_dev, int reg, u32 val)
{
	unsigned long lock_flags = 0;

	spin_lock_irqsave(&hdmirx_dev->dma_rst_lock,  lock_flags);
	writel(val, hdmirx_dev->regs + reg);
	spin_unlock_irqrestore(&hdmirx_dev->dma_rst_lock, lock_flags);
}

static u32 hdmirx_readl(struct rk_hdmirx_dev *hdmirx_dev, int reg)
{
	unsigned long lock_flags = 0;
	u32 val;

	spin_lock_irqsave(&hdmirx_dev->dma_rst_lock,  lock_flags);
	val = readl(hdmirx_dev->regs + reg);
	spin_unlock_irqrestore(&hdmirx_dev->dma_rst_lock, lock_flags);
	return val;
}

static void hdmirx_reset_dma(struct rk_hdmirx_dev *hdmirx_dev)
{
	unsigned long lock_flags = 0;

	spin_lock_irqsave(&hdmirx_dev->dma_rst_lock,  lock_flags);
	reset_control_assert(hdmirx_dev->rst_a);
	reset_control_deassert(hdmirx_dev->rst_a);
	spin_unlock_irqrestore(&hdmirx_dev->dma_rst_lock, lock_flags);
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
		hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_BGR24;
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
		hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_BGR24;
		break;
	}

	v4l2_dbg(1, debug, v4l2_dev, "%s: pix_fmt: %s\n", __func__,
			pix_fmt_str[hdmirx_dev->pix_fmt]);
}

static void hdmirx_get_timings(struct rk_hdmirx_dev *hdmirx_dev,
			       struct v4l2_bt_timings *bt, bool from_dma)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	u32 hact, vact, htotal, vtotal, fps;
	u32 hfp, hs, hbp, vfp, vs, vbp;
	u32 val;

	if (from_dma) {
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
		hfp = htotal - hact - hs - hbp;
		vfp = vtotal - vact - vs - vbp;
	} else {
		val = hdmirx_readl(hdmirx_dev, VMON_STATUS1);
		hs = (val >> 16) & 0xffff;
		hfp = val & 0xffff;
		val = hdmirx_readl(hdmirx_dev, VMON_STATUS2);
		hbp = val & 0xffff;
		val = hdmirx_readl(hdmirx_dev, VMON_STATUS3);
		htotal = (val >> 16) & 0xffff;
		hact = val & 0xffff;
		val = hdmirx_readl(hdmirx_dev, VMON_STATUS4);
		vs = (val >> 16) & 0xffff;
		vfp = val & 0xffff;
		val = hdmirx_readl(hdmirx_dev, VMON_STATUS5);
		vbp = val & 0xffff;
		val = hdmirx_readl(hdmirx_dev, VMON_STATUS6);
		vtotal = (val >> 16) & 0xffff;
		vact = val & 0xffff;
		if (hdmirx_dev->pix_fmt == HDMIRX_YUV420)
			hact *= 2;
	}
	if (hdmirx_dev->pix_fmt == HDMIRX_YUV420)
		htotal *= 2;
	fps = (bt->pixelclock + (htotal * vtotal) / 2) / (htotal * vtotal);
	if (hdmirx_dev->pix_fmt == HDMIRX_YUV420)
		fps *= 2;
	bt->width = hact;
	bt->height = vact;
	bt->hfrontporch = hfp;
	bt->hsync = hs;
	bt->hbackporch = hbp;
	bt->vfrontporch = vfp;
	bt->vsync = vs;
	bt->vbackporch = vbp;

	v4l2_dbg(1, debug, v4l2_dev, "get timings from %s\n", from_dma ? "dma" : "ctrl");
	v4l2_dbg(1, debug, v4l2_dev,
		 "act:%ux%u, total:%ux%u, fps:%u, pixclk:%llu\n",
		 bt->width, bt->height, htotal, vtotal, fps, bt->pixelclock);

	v4l2_dbg(2, debug, v4l2_dev,
		 "hfp:%u, hs:%u, hbp:%u, vfp:%u, vs:%u, vbp:%u\n",
		 bt->hfrontporch, bt->hsync, bt->hbackporch,
		 bt->vfrontporch, bt->vsync, bt->vbackporch);
}

static bool hdmirx_check_timing_valid(struct v4l2_bt_timings *bt)
{
	if (bt->width < 100 || bt->width > 5000 ||
	    bt->height < 100 || bt->height > 5000)
		return false;

	if (bt->hsync == 0 || bt->hsync > 200 ||
	    bt->vsync == 0 || bt->vsync > 100)
		return false;

	if (bt->hbackporch == 0 || bt->hbackporch > 2000 ||
	    bt->vbackporch == 0 || bt->vbackporch > 2000)
		return false;

	if (bt->hfrontporch == 0 || bt->hfrontporch > 2000 ||
	    bt->vfrontporch == 0 || bt->vfrontporch > 2000)
		return false;

	return true;
}

static int hdmirx_get_detected_timings(struct rk_hdmirx_dev *hdmirx_dev,
		struct v4l2_dv_timings *timings, bool from_dma)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct v4l2_bt_timings *bt = &timings->bt;
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
	bt->pixelclock = pix_clk;

	hdmirx_get_timings(hdmirx_dev, bt, from_dma);
	if (bt->interlaced == V4L2_DV_INTERLACED) {
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
	}

	v4l2_dbg(2, debug, v4l2_dev, "tmds_clk:%llu\n", tmds_clk);
	v4l2_dbg(1, debug, v4l2_dev, "interlace:%d, fmt:%d, vic:%d, color:%d, mode:%s\n",
		 bt->interlaced, hdmirx_dev->pix_fmt,
		 hdmirx_dev->cur_vic, hdmirx_dev->color_depth,
		 hdmirx_dev->is_dvi_mode ? "dvi" : "hdmi");
	v4l2_dbg(2, debug, v4l2_dev, "deframer_st:%#x\n", deframer_st);

	if (!hdmirx_check_timing_valid(bt))
		return -EINVAL;

	return 0;
}

static void hdmirx_set_negative_pol(struct rk_hdmirx_dev *hdmirx_dev, bool en)
{
	if (en) {
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
		return;
	}

	hdmirx_update_bits(hdmirx_dev, DMA_CONFIG6,
				VSYNC_TOGGLE_EN|
				HSYNC_TOGGLE_EN,
				0);

	hdmirx_update_bits(hdmirx_dev, VIDEO_CONFIG2,
				VPROC_VSYNC_POL_OVR_VALUE|
				VPROC_VSYNC_POL_OVR_EN|
				VPROC_HSYNC_POL_OVR_VALUE|
				VPROC_HSYNC_POL_OVR_EN,
				0);
}

static int hdmirx_try_to_get_timings(struct rk_hdmirx_dev *hdmirx_dev,
		struct v4l2_dv_timings *timings, int try_cnt)
{
	int i, cnt = 0, fail_cnt = 0, ret = 0;
	bool from_dma = false;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	hdmirx_set_negative_pol(hdmirx_dev, false);
	for (i = 0; i < try_cnt; i++) {
		ret = hdmirx_get_detected_timings(hdmirx_dev, timings, from_dma);
		if (ret) {
			cnt = 0;
			fail_cnt++;
			if (fail_cnt > 3) {
				hdmirx_set_negative_pol(hdmirx_dev, true);
				from_dma = true;
			}
		} else {
			cnt++;
		}

		if (cnt >= 5)
			break;

		usleep_range(10*1000, 10*1100);
	}

	if (try_cnt > 8 && cnt < 5)
		v4l2_dbg(1, debug, v4l2_dev, "%s: res not stable!\n", __func__);

	return ret;
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

	/*
	 * query dv timings is during preview, dma's timing is stable,
	 * so we can get from DMA. If the current resolution is negative,
	 * get timing from CTRL need to change polarity of sync,
	 * maybe cause DMA errors.
	 */
	ret = hdmirx_get_detected_timings(hdmirx_dev, timings, true);
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

	memset(&hdmirx_dev->edid, 0, sizeof(hdmirx_dev->edid));
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
	memcpy(&hdmirx_dev->edid, edid->edid, edid->blocks * EDID_BLOCK_SIZE);
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

	disable_irq(hdmirx_dev->hdmi_irq);
	disable_irq(hdmirx_dev->dma_irq);
	sip_fiq_control(RK_SIP_FIQ_CTRL_FIQ_DIS, RK_IRQ_HDMIRX_HDMI, 0);

	if (tx_5v_power_present(hdmirx_dev))
		hdmirx_plugout(hdmirx_dev);
	hdmirx_write_edid(hdmirx_dev, edid, false);
	hdmirx_dev->edid_version = HDMIRX_EDID_USER;

	enable_irq(hdmirx_dev->hdmi_irq);
	enable_irq(hdmirx_dev->dma_irq);
	sip_fiq_control(RK_SIP_FIQ_CTRL_FIQ_EN, RK_IRQ_HDMIRX_HDMI, 0);
	schedule_delayed_work_on(hdmirx_dev->bound_cpu,
				 &hdmirx_dev->delayed_work_hotplug,
				 msecs_to_jiffies(500));

	return 0;
}

static int hdmirx_get_edid(struct file *file, void *fh,
		struct v4l2_edid *edid)
{
	struct hdmirx_stream *stream = video_drvdata(file);
	struct rk_hdmirx_dev *hdmirx_dev = stream->hdmirx_dev;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

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

	memcpy(edid->edid, &hdmirx_dev->edid, edid->blocks * EDID_BLOCK_SIZE);

	v4l2_dbg(1, debug, v4l2_dev, "%s: Read EDID: =====\n", __func__);
	if (debug > 0)
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1,
			edid->edid, edid->blocks * EDID_BLOCK_SIZE, false);

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

static void hdmirx_register_hdcp(struct device *dev,
				 struct rk_hdmirx_dev *hdmirx_dev,
				 bool hdcp1x_enable)
{
	struct rk_hdmirx_hdcp hdmirx_hdcp = {
		.hdmirx = hdmirx_dev,
		.write = hdmirx_writel,
		.read = hdmirx_readl,
		.enable = hdcp1x_enable,
		.dev = hdmirx_dev->dev,
	};

	hdmirx_dev->hdcp = rk_hdmirx_hdcp_register(&hdmirx_hdcp);
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

static int hdmirx_phy_register_read(struct rk_hdmirx_dev *hdmirx_dev,
		u32 phy_reg, u32 *val)
{
	u32 i;
	struct device *dev = hdmirx_dev->dev;

	hdmirx_dev->cr_read_done = false;
	/* clear irq status */
	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_CLEAR, 0xffffffff);
	/* en irq */
	hdmirx_update_bits(hdmirx_dev, MAINUNIT_2_INT_MASK_N,
			PHYCREG_CR_READ_DONE, PHYCREG_CR_READ_DONE);
	/* write phy reg addr */
	hdmirx_writel(hdmirx_dev, PHYCREG_CONFIG1, phy_reg);
	/* config read enable */
	hdmirx_writel(hdmirx_dev, PHYCREG_CONTROL, PHYCREG_CR_PARA_READ_P);

	for (i = 0; i < 50; i++) {
		usleep_range(200, 210);
		if (hdmirx_dev->cr_read_done)
			break;
	}

	if (i == 50) {
		dev_err(dev, "%s wait cr read done failed!\n", __func__);
		return -1;
	}

	/* read phy reg val */
	*val = hdmirx_readl(hdmirx_dev, PHYCREG_STATUS);

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

static void hdmirx_tmds_clk_ratio_config(struct rk_hdmirx_dev *hdmirx_dev)
{
	u32 val;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	val = hdmirx_readl(hdmirx_dev, SCDC_REGBANK_STATUS1);
	v4l2_dbg(3, debug, v4l2_dev, "%s scdc_regbank_st:%#x\n", __func__, val);
	hdmirx_dev->tmds_clk_ratio = (val & SCDC_TMDSBITCLKRATIO) > 0;

	if (hdmirx_dev->tmds_clk_ratio) {
		v4l2_dbg(3, debug, v4l2_dev, "%s HDMITX greater than 3.4Gbps!\n", __func__);
		hdmirx_update_bits(hdmirx_dev, PHY_CONFIG,
				   TMDS_CLOCK_RATIO, TMDS_CLOCK_RATIO);
	} else {
		v4l2_dbg(3, debug, v4l2_dev, "%s HDMITX less than 3.4Gbps!\n", __func__);
		hdmirx_update_bits(hdmirx_dev, PHY_CONFIG,
				   TMDS_CLOCK_RATIO, 0);
	}
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

	hdmirx_tmds_clk_ratio_config(hdmirx_dev);
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
	hdmirx_update_bits(hdmirx_dev, DEFRAMER_CONFIG0,
			   VS_REMAPFILTER_EN_QST | VS_FILTER_ORDER_QST_MASK,
			   VS_REMAPFILTER_EN_QST | VS_FILTER_ORDER_QST(0x3));
}

static void hdmirx_format_change(struct rk_hdmirx_dev *hdmirx_dev)
{
	struct v4l2_dv_timings timings;
	struct hdmirx_stream *stream = &hdmirx_dev->stream;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	const struct v4l2_event ev_src_chg = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	if (hdmirx_try_to_get_timings(hdmirx_dev, &timings, 20)) {
		schedule_delayed_work_on(hdmirx_dev->bound_cpu,
				&hdmirx_dev->delayed_work_hotplug,
				msecs_to_jiffies(20));
		return;
	}

	if (!v4l2_match_dv_timings(&hdmirx_dev->timings, &timings, 0, false)) {
		/* automatically set timing rather than set by userspace */
		hdmirx_dev->timings = timings;
		v4l2_print_dv_timings(hdmirx_dev->v4l2_dev.name,
				"hdmirx_format_change: New format: ",
				&timings, false);
	}

	hdmirx_dev->get_timing = true;
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

		hdmirx_tmds_clk_ratio_config(hdmirx_dev);
	}

	if (i == 300) {
		v4l2_err(v4l2_dev, "%s signal not lock, tmds_clk_ratio:%d\n",
				__func__, hdmirx_dev->tmds_clk_ratio);
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
	hdmirx_update_bits(hdmirx_dev, HDCP2_CONFIG,
			   HDCP2_SWITCH_OVR_VALUE |
			   HDCP2_SWITCH_OVR_EN,
			   HDCP2_SWITCH_OVR_EN);
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
		case V4L2_PIX_FMT_BGR24:
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
	int line_flag;

	if (!hdmirx_dev->get_timing) {
		v4l2_err(v4l2_dev, "Err, timing is invalid\n");
		return 0;
	}

	mutex_lock(&hdmirx_dev->stream_lock);
	stream->frame_idx = 0;
	stream->line_flag_int_cnt = 0;
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

	if (bt->height) {
		if (bt->interlaced == V4L2_DV_INTERLACED)
			line_flag = bt->height / 4;
		else
			line_flag = bt->height / 2;
		hdmirx_update_bits(hdmirx_dev, DMA_CONFIG7,
				LINE_FLAG_NUM_MASK,
				LINE_FLAG_NUM(line_flag));
	} else {
		v4l2_err(v4l2_dev, "height err: %d\n", bt->height);
	}

	hdmirx_writel(hdmirx_dev, DMA_CONFIG5, 0xffffffff);
	hdmirx_writel(hdmirx_dev, CED_DYN_CONTROL, 0x1);
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
	hdmirx_reset_dma(hdmirx_dev);
	hdmirx_dev->get_timing = false;
	schedule_delayed_work_on(hdmirx_dev->bound_cpu,
			&hdmirx_dev->delayed_work_res_change,
			msecs_to_jiffies(50));
}

static void avpunit_0_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
				  int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	if (status & (CED_DYN_CNT_CH2_IRQ |
		      CED_DYN_CNT_CH1_IRQ |
		      CED_DYN_CNT_CH0_IRQ)) {
		process_signal_change(hdmirx_dev);
		v4l2_dbg(2, debug, v4l2_dev, "%s: avp0_st:%#x\n",
				__func__, status);
		*handled = true;
	}

	hdmirx_writel(hdmirx_dev, AVPUNIT_0_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, AVPUNIT_0_INT_FORCE, 0x0);
}

static void avpunit_1_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
				  int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	if (status & DEFRAMER_VSYNC_THR_REACHED_IRQ) {
		v4l2_info(v4l2_dev, "Vertical Sync threshold reached interrupt %#x", status);
		hdmirx_update_bits(hdmirx_dev, AVPUNIT_1_INT_MASK_N,
				   DEFRAMER_VSYNC_THR_REACHED_MASK_N,
				   0);
		schedule_delayed_work_on(hdmirx_dev->bound_cpu,
				&hdmirx_dev->delayed_work_audio, HZ / 2);
		*handled = true;
	}
}
static void mainunit_0_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
		int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	v4l2_dbg(2, debug, v4l2_dev, "mu0_st:%#x\n", status);
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
	hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_FORCE, 0x0);
}

static void mainunit_2_int_handler(struct rk_hdmirx_dev *hdmirx_dev,
		int status, bool *handled)
{
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	v4l2_dbg(2, debug, v4l2_dev, "mu2_st:%#x\n", status);
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
		v4l2_dbg(2, debug, v4l2_dev, "%s: TMDSVALID_STABLE_CHG\n", __func__);
		*handled = true;
	}

	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_CLEAR, 0xffffffff);
	hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_FORCE, 0x0);
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

	v4l2_dbg(2, debug, v4l2_dev, "%s scdc_st:%#x\n", __func__, status);
	if (status & SCDCTMDSCCFG_CHG) {
		hdmirx_tmds_clk_ratio_config(hdmirx_dev);
		*handled = true;
	}

	hdmirx_writel(hdmirx_dev, SCDC_INT_CLEAR, 0xffffffff);
}

static irqreturn_t hdmirx_hdmi_irq_handler(int irq, void *dev_id)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_id;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	bool handled = false;
	u32 mu0_st, mu2_st, pk2_st, scdc_st, avp1_st, avp0_st;
	u32 mu0_mask, mu2_mask, pk2_mask, scdc_mask, avp1_msk, avp0_msk;

	mu0_mask = hdmirx_readl(hdmirx_dev, MAINUNIT_0_INT_MASK_N);
	mu2_mask = hdmirx_readl(hdmirx_dev, MAINUNIT_2_INT_MASK_N);
	pk2_mask = hdmirx_readl(hdmirx_dev, PKT_2_INT_MASK_N);
	scdc_mask = hdmirx_readl(hdmirx_dev, SCDC_INT_MASK_N);
	mu0_st = hdmirx_readl(hdmirx_dev, MAINUNIT_0_INT_STATUS);
	mu2_st = hdmirx_readl(hdmirx_dev, MAINUNIT_2_INT_STATUS);
	pk2_st = hdmirx_readl(hdmirx_dev, PKT_2_INT_STATUS);
	scdc_st = hdmirx_readl(hdmirx_dev, SCDC_INT_STATUS);
	avp0_st = hdmirx_readl(hdmirx_dev, AVPUNIT_0_INT_STATUS);
	avp1_st = hdmirx_readl(hdmirx_dev, AVPUNIT_1_INT_STATUS);
	avp0_msk = hdmirx_readl(hdmirx_dev, AVPUNIT_0_INT_MASK_N);
	avp1_msk = hdmirx_readl(hdmirx_dev, AVPUNIT_1_INT_MASK_N);
	mu0_st &= mu0_mask;
	mu2_st &= mu2_mask;
	pk2_st &= pk2_mask;
	avp1_st &= avp1_msk;
	avp0_st &= avp0_msk;
	scdc_st &= scdc_mask;

	if (avp0_st)
		avpunit_0_int_handler(hdmirx_dev, avp0_st, &handled);
	if (avp1_st)
		avpunit_1_int_handler(hdmirx_dev, avp1_st, &handled);
	if (mu0_st)
		mainunit_0_int_handler(hdmirx_dev, mu0_st, &handled);
	if (mu2_st)
		mainunit_2_int_handler(hdmirx_dev, mu2_st, &handled);
	if (pk2_st)
		pkt_2_int_handler(hdmirx_dev, pk2_st, &handled);
	if (scdc_st)
		scdc_int_handler(hdmirx_dev, scdc_st, &handled);

	if (!handled) {
		v4l2_dbg(2, debug, v4l2_dev, "%s: hdmi irq not handled!", __func__);
		v4l2_dbg(2, debug, v4l2_dev,
			 "avp0:%#x, avp1:%#x, mu0:%#x, mu2:%#x, pk2:%#x, scdc:%#x\n",
			 avp0_st, avp1_st, mu0_st, mu2_st, pk2_st, scdc_st);
	}

	v4l2_dbg(2, debug, v4l2_dev, "%s: en_fiq", __func__);
	sip_fiq_control(RK_SIP_FIQ_CTRL_FIQ_EN, RK_IRQ_HDMIRX_HDMI, 0);

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

	if (!(stream->irq_stat) && !(stream->irq_stat & LINE_FLAG_INT_EN))
		v4l2_dbg(1, debug, v4l2_dev,
			 "%s: last time have no line_flag_irq\n", __func__);

	if (stream->line_flag_int_cnt <= FILTER_FRAME_CNT)
		goto DMA_IDLE_OUT;

	if ((bt->interlaced != V4L2_DV_INTERLACED) ||
			(stream->line_flag_int_cnt % 2 == 0)) {
		if (stream->next_buf) {
			if (stream->curr_buf)
				vb_done = &stream->curr_buf->vb;

			if (vb_done) {
				vb_done->vb2_buf.timestamp = ktime_get_ns();
				vb_done->sequence = stream->frame_idx;
				hdmirx_vb_done(stream, vb_done);
				stream->frame_idx++;
				if (stream->frame_idx == 30)
					v4l2_info(v4l2_dev, "rcv frames\n");
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

DMA_IDLE_OUT:
	*handled = true;
}

static void line_flag_int_handler(struct rk_hdmirx_dev *hdmirx_dev, bool *handled)
{
	struct hdmirx_stream *stream = &hdmirx_dev->stream;
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	struct v4l2_dv_timings timings = hdmirx_dev->timings;
	struct v4l2_bt_timings *bt = &timings.bt;
	u32 dma_cfg6;

	stream->line_flag_int_cnt++;
	if (!(stream->irq_stat) && !(stream->irq_stat & HDMIRX_DMA_IDLE_INT))
		v4l2_dbg(1, debug, v4l2_dev,
			 "%s: last have no dma_idle_irq\n", __func__);
	dma_cfg6 = hdmirx_readl(hdmirx_dev, DMA_CONFIG6);
	if (!(dma_cfg6 & HDMIRX_DMA_EN)) {
		v4l2_dbg(2, debug, v4l2_dev, "%s: dma not on\n", __func__);
		goto LINE_FLAG_OUT;
	}

	if (stream->line_flag_int_cnt <= FILTER_FRAME_CNT)
		goto LINE_FLAG_OUT;

	if ((bt->interlaced != V4L2_DV_INTERLACED) ||
			(stream->line_flag_int_cnt % 2 == 0)) {
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
		v4l2_dbg(3, debug, v4l2_dev, "%s: interlace:%d, line_flag_int_cnt:%d\n",
			 __func__, bt->interlaced, stream->line_flag_int_cnt);
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
		v4l2_dbg(3, debug, v4l2_dev,
			 "%s: dma irq not handled, dma_stat1:%#x!\n",
			 __func__, dma_stat1);

	stream->irq_stat = dma_stat1;
	hdmirx_writel(hdmirx_dev, DMA_CONFIG5, 0xffffffff);

	return IRQ_HANDLED;
}

static void hdmirx_audio_interrupts_setup(struct rk_hdmirx_dev *hdmirx_dev, bool en)
{
	dev_info(hdmirx_dev->dev, "%s: %d", __func__, en);
	if (en) {
		hdmirx_update_bits(hdmirx_dev, AVPUNIT_1_INT_MASK_N,
				   DEFRAMER_VSYNC_THR_REACHED_MASK_N,
				   DEFRAMER_VSYNC_THR_REACHED_MASK_N);
	} else {
		hdmirx_update_bits(hdmirx_dev, AVPUNIT_1_INT_MASK_N,
				   DEFRAMER_VSYNC_THR_REACHED_MASK_N,
				   0);
	}
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
		hdmirx_update_bits(hdmirx_dev, AVPUNIT_0_INT_MASK_N,
				   CED_DYN_CNT_CH2_IRQ |
				   CED_DYN_CNT_CH1_IRQ |
				   CED_DYN_CNT_CH0_IRQ,
				   CED_DYN_CNT_CH2_IRQ |
				   CED_DYN_CNT_CH1_IRQ |
				   CED_DYN_CNT_CH0_IRQ);
	} else {
		hdmirx_writel(hdmirx_dev, MAINUNIT_0_INT_MASK_N, 0);
		hdmirx_writel(hdmirx_dev, MAINUNIT_2_INT_MASK_N, 0);
		hdmirx_writel(hdmirx_dev, AVPUNIT_0_INT_MASK_N, 0);
	}
}

static void hdmirx_plugin(struct rk_hdmirx_dev *hdmirx_dev)
{
	int ret;

	cpu_latency_qos_update_request(&hdmirx_dev->pm_qos, 0);
	schedule_delayed_work_on(hdmirx_dev->bound_cpu,
		&hdmirx_dev->delayed_work_heartbeat, msecs_to_jiffies(10));
	sip_wdt_config(WDT_START, 0, 0, 0);
	hdmirx_set_cpu_limit_freq(hdmirx_dev);
	hdmirx_submodule_init(hdmirx_dev);
	hdmirx_update_bits(hdmirx_dev, SCDC_CONFIG, POWERPROVIDED,
				POWERPROVIDED);
	hdmirx_hpd_ctrl(hdmirx_dev, true);
	hdmirx_phy_config(hdmirx_dev);
	hdmirx_audio_setup(hdmirx_dev);
	ret = hdmirx_wait_lock_and_get_timing(hdmirx_dev);
	if (ret) {
		hdmirx_plugout(hdmirx_dev);
		schedule_delayed_work_on(hdmirx_dev->bound_cpu,
					 &hdmirx_dev->delayed_work_hotplug,
					 msecs_to_jiffies(200));
		return;
	}
	hdmirx_dma_config(hdmirx_dev);
	hdmirx_interrupts_setup(hdmirx_dev, true);
	hdmirx_audio_handle_plugged_change(hdmirx_dev, 1);
	if (hdmirx_dev->hdcp && hdmirx_dev->hdcp->hdcp_start)
		hdmirx_dev->hdcp->hdcp_start(hdmirx_dev->hdcp);
}

static void hdmirx_plugout(struct rk_hdmirx_dev *hdmirx_dev)
{
	hdmirx_audio_handle_plugged_change(hdmirx_dev, 0);
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
	hdmirx_reset_dma(hdmirx_dev);
	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG,
			HDMI_DISABLE | PHY_RESET | PHY_PDDQ,
			HDMI_DISABLE);
	hdmirx_writel(hdmirx_dev, PHYCREG_CONFIG0, 0x0);
	cancel_delayed_work(&hdmirx_dev->delayed_work_res_change);
	cancel_delayed_work(&hdmirx_dev->delayed_work_audio);
	cpu_latency_qos_update_request(&hdmirx_dev->pm_qos, PM_QOS_DEFAULT_VALUE);
	hdmirx_cancel_cpu_limit_freq(hdmirx_dev);
	cancel_delayed_work(&hdmirx_dev->delayed_work_heartbeat);
	flush_work(&hdmirx_dev->work_wdt_config);
	sip_wdt_config(WDT_STOP, 0, 0, 0);
}

static void hdmirx_delayed_work_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk_hdmirx_dev *hdmirx_dev = container_of(dwork,
			struct rk_hdmirx_dev, delayed_work_hotplug);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	bool plugin;

	mutex_lock(&hdmirx_dev->work_lock);
	hdmirx_dev->get_timing = false;
	plugin = tx_5v_power_present(hdmirx_dev);
	v4l2_ctrl_s_ctrl(hdmirx_dev->detect_tx_5v_ctrl, plugin);
	v4l2_dbg(1, debug, v4l2_dev, "%s: plugin:%d\n", __func__, plugin);

	if (plugin)
		hdmirx_plugin(hdmirx_dev);
	else
		hdmirx_plugout(hdmirx_dev);

	mutex_unlock(&hdmirx_dev->work_lock);
}

static u32 hdmirx_audio_ch(struct rk_hdmirx_dev *hdmirx_dev)
{
	u32 acr_pb3_0, acr_pb7_4, ch, ca;

	hdmirx_readl(hdmirx_dev, PKTDEC_AUDIF_PH2_1);
	acr_pb3_0 = hdmirx_readl(hdmirx_dev, PKTDEC_AUDIF_PB3_0);
	acr_pb7_4 =  hdmirx_readl(hdmirx_dev, PKTDEC_AUDIF_PB7_4);
	ca = acr_pb7_4 & 0xff;
	ch = ((acr_pb3_0>>8) & 0x07) + 1;
	dev_dbg(hdmirx_dev->dev, "%s: acr_pb3_0=%#x; ch=%u; ca=%#x\n",
		__func__, acr_pb3_0, ch, ca);
	return ch;
}

static u32 hdmirx_audio_fs(struct rk_hdmirx_dev *hdmirx_dev)
{
	u64 tmds_clk, fs_audio = 0;
	u32 acr_cts, acr_n, tmdsqpclk_freq;
	u32 acr_pb7_4, acr_pb3_0;

	tmdsqpclk_freq = hdmirx_readl(hdmirx_dev, CMU_TMDSQPCLK_FREQ);
	hdmirx_readl(hdmirx_dev, PKTDEC_ACR_PH2_1);
	acr_pb7_4 = hdmirx_readl(hdmirx_dev, PKTDEC_ACR_PB3_0);
	acr_pb3_0 = hdmirx_readl(hdmirx_dev, PKTDEC_ACR_PB7_4);
	acr_cts = __be32_to_cpu(acr_pb7_4) & 0xfffff;
	acr_n = (__be32_to_cpu(acr_pb3_0) & 0x0fffff00) >> 8;
	tmds_clk = tmdsqpclk_freq * 4 * 1000U;
	if (acr_cts != 0) {
		fs_audio = div_u64((tmds_clk * acr_n), acr_cts);
		fs_audio /= 128;
		fs_audio = div_u64(fs_audio + 50, 100);
		fs_audio *= 100;
	}
	dev_dbg(hdmirx_dev->dev, "%s: fs_audio=%llu; acr_cts=%u; acr_n=%u\n",
		__func__, fs_audio, acr_cts, acr_n);
	return fs_audio;
}

static void hdmirx_audio_set_ch(struct rk_hdmirx_dev *hdmirx_dev, u32 ch_audio)
{
	hdmirx_dev->audio_state.ch_audio = ch_audio;
}

static void hdmirx_audio_set_fs(struct rk_hdmirx_dev *hdmirx_dev, u32 fs_audio)
{
	u32 hdmirx_aud_clkrate_t = fs_audio*128;

	dev_dbg(hdmirx_dev->dev, "%s: %u to %u with fs %u\n", __func__,
		hdmirx_dev->audio_state.hdmirx_aud_clkrate, hdmirx_aud_clkrate_t,
		fs_audio);
	clk_set_rate(hdmirx_dev->clks[1].clk, hdmirx_aud_clkrate_t);
	hdmirx_dev->audio_state.hdmirx_aud_clkrate = hdmirx_aud_clkrate_t;
	hdmirx_dev->audio_state.fs_audio = fs_audio;
}

static void hdmirx_audio_fifo_init(struct rk_hdmirx_dev *hdmirx_dev)
{
	dev_info(hdmirx_dev->dev, "%s\n", __func__);
	hdmirx_writel(hdmirx_dev, AUDIO_FIFO_CONTROL, 1);
	usleep_range(200, 210);
	hdmirx_writel(hdmirx_dev, AUDIO_FIFO_CONTROL, 0);
}

static void hdmirx_audio_clk_ppm_inc(struct rk_hdmirx_dev *hdmirx_dev, int ppm)
{
	int delta, rate, inc;

	rate = hdmirx_dev->audio_state.hdmirx_aud_clkrate;
	if (ppm < 0) {
		ppm = -ppm;
		inc = -1;
	} else
		inc = 1;
	delta = (int)div64_u64((uint64_t)rate * ppm + 500000, 1000000);
	delta *= inc;
	rate = hdmirx_dev->audio_state.hdmirx_aud_clkrate + delta;
	dev_dbg(hdmirx_dev->dev, "%s: %u to %u(delta:%d)\n",
		__func__, hdmirx_dev->audio_state.hdmirx_aud_clkrate, rate, delta);
	clk_set_rate(hdmirx_dev->clks[1].clk, rate);
	hdmirx_dev->audio_state.hdmirx_aud_clkrate = rate;
}

static void hdmirx_audio_setup(struct rk_hdmirx_dev *hdmirx_dev)
{
	struct hdmirx_audiostate *as = &hdmirx_dev->audio_state;

	as->ctsn_flag = 0;
	as->fs_audio = 0;
	as->ch_audio = 0;
	as->pre_state = 0;
	as->init_state = INIT_FIFO_STATE*4;
	as->fifo_int = false;
	as->audio_enabled = false;
	hdmirx_audio_set_fs(hdmirx_dev, 44100);
	/* Disable audio domain */
	hdmirx_update_bits(hdmirx_dev, GLOBAL_SWENABLE, AUDIO_ENABLE, 0);
	/* Configure Vsync interrupt threshold */
	hdmirx_update_bits(hdmirx_dev, DEFRAMER_CONFIG0, VS_CNT_THR_QST_MASK, VS_CNT_THR_QST(3));
	hdmirx_audio_interrupts_setup(hdmirx_dev, true);
	hdmirx_writel(hdmirx_dev, DEFRAMER_VSYNC_CNT_CLEAR, VSYNC_CNT_CLR_P);
	hdmirx_writel(hdmirx_dev, AVPUNIT_1_INT_CLEAR, DEFRAMER_VSYNC_THR_REACHED_CLEAR);
	hdmirx_writel(hdmirx_dev, AUDIO_FIFO_THR_PASS, INIT_FIFO_STATE);
	hdmirx_writel(hdmirx_dev, AUDIO_FIFO_THR,
		      AFIFO_THR_LOW_QST(0x20) | AFIFO_THR_HIGH_QST(0x160));
	hdmirx_writel(hdmirx_dev, AUDIO_FIFO_MUTE_THR,
		      AFIFO_THR_MUTE_LOW_QST(0x8) | AFIFO_THR_MUTE_HIGH_QST(0x178));
}

static int hdmirx_audio_hw_params(struct device *dev, void *data,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static int hdmirx_audio_startup(struct device *dev, void *data)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);

	if (tx_5v_power_present(hdmirx_dev))
		return 0;
	dev_err(dev, "%s: device is no connected\n", __func__);
	return -ENODEV;
}

static void hdmirx_audio_shutdown(struct device *dev, void *data)
{
	dev_dbg(dev, "%s\n", __func__);
}

static int hdmirx_audio_get_dai_id(struct snd_soc_component *comment,
				   struct device_node *endpoint)
{
	dev_dbg(comment->dev, "%s\n", __func__);
	return 0;
}

static void hdmirx_audio_handle_plugged_change(struct rk_hdmirx_dev *hdmirx_dev, bool plugged)
{
	if (hdmirx_dev->plugged_cb && hdmirx_dev->codec_dev)
		hdmirx_dev->plugged_cb(hdmirx_dev->codec_dev, plugged);
}

static int hdmirx_audio_hook_plugged_cb(struct device *dev, void *data,
					hdmi_codec_plugged_cb fn,
					struct device *codec_dev)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	mutex_lock(&hdmirx_dev->work_lock);
	hdmirx_dev->plugged_cb = fn;
	hdmirx_dev->codec_dev = codec_dev;
	hdmirx_audio_handle_plugged_change(hdmirx_dev, tx_5v_power_present(hdmirx_dev));
	mutex_unlock(&hdmirx_dev->work_lock);
	return 0;
}

static const struct hdmi_codec_ops hdmirx_audio_codec_ops = {
	.hw_params = hdmirx_audio_hw_params,
	.audio_startup = hdmirx_audio_startup,
	.audio_shutdown = hdmirx_audio_shutdown,
	.get_dai_id = hdmirx_audio_get_dai_id,
	.hook_plugged_cb = hdmirx_audio_hook_plugged_cb
};

static int hdmirx_register_audio_device(struct rk_hdmirx_dev *hdmirx_dev)
{
	struct hdmirx_audiostate *as = &hdmirx_dev->audio_state;
	struct hdmi_codec_pdata codec_data = {
		.ops = &hdmirx_audio_codec_ops,
		.spdif = 1,
		.i2s = 1,
		.max_i2s_channels = 8,
		.data = hdmirx_dev,
	};

	as->pdev = platform_device_register_data(hdmirx_dev->dev,
						 HDMI_CODEC_DRV_NAME,
						 PLATFORM_DEVID_AUTO,
						 &codec_data,
						 sizeof(codec_data));

	return PTR_ERR_OR_ZERO(as->pdev);
}

static void hdmirx_unregister_audio_device(void *data)
{
	struct rk_hdmirx_dev *hdmirx_dev = data;
	struct hdmirx_audiostate *as = &hdmirx_dev->audio_state;

	if (as->pdev) {
		platform_device_unregister(as->pdev);
		as->pdev = NULL;
	}
}

static void hdmirx_unregister_class_device(void *data)
{
	struct rk_hdmirx_dev *hdmirx_dev = data;
	struct device *dev = hdmirx_dev->classdev;

	device_unregister(dev);
}

static const char *audio_fifo_err(u32 fifo_status)
{
	switch (fifo_status & (AFIFO_UNDERFLOW_ST | AFIFO_OVERFLOW_ST)) {
	case AFIFO_UNDERFLOW_ST:
		return "underflow";
	case AFIFO_OVERFLOW_ST:
		return "overflow";
	case AFIFO_UNDERFLOW_ST | AFIFO_OVERFLOW_ST:
		return "underflow and overflow";
	}
	return "underflow or overflow";
}

static void hdmirx_enable_audio_output(struct rk_hdmirx_dev *hdmirx_dev,
				      int ch_audio, int fs_audio, int spdif)
{
	if (spdif) {
		dev_warn(hdmirx_dev->dev, "We don't recommend using spdif\n");
	} else {
		if (ch_audio > 2) {
			hdmirx_update_bits(hdmirx_dev, AUDIO_PROC_CONFIG0,
					   SPEAKER_ALLOC_OVR_EN | I2S_EN,
					   SPEAKER_ALLOC_OVR_EN | I2S_EN);
			hdmirx_writel(hdmirx_dev, AUDIO_PROC_CONFIG3, 0xffffffff);
		} else {
			hdmirx_update_bits(hdmirx_dev, AUDIO_PROC_CONFIG0,
					   SPEAKER_ALLOC_OVR_EN | I2S_EN, I2S_EN);
		}
	}
}

static void hdmirx_delayed_work_audio(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk_hdmirx_dev *hdmirx_dev = container_of(dwork,
							struct rk_hdmirx_dev,
							delayed_work_audio);
	struct hdmirx_audiostate *as = &hdmirx_dev->audio_state;
	u32 fs_audio, ch_audio;
	int cur_state, init_state, pre_state, fifo_status2;
	unsigned long delay = 200;

	if (!as->audio_enabled) {
		dev_info(hdmirx_dev->dev, "%s: enable audio\n", __func__);
		hdmirx_update_bits(hdmirx_dev, GLOBAL_SWENABLE, AUDIO_ENABLE, AUDIO_ENABLE);
		hdmirx_writel(hdmirx_dev, GLOBAL_SWRESET_REQUEST, AUDIO_SWRESETREQ);
		as->audio_enabled = true;
	}
	fs_audio = hdmirx_audio_fs(hdmirx_dev);
	ch_audio = hdmirx_audio_ch(hdmirx_dev);
	fifo_status2 =  hdmirx_readl(hdmirx_dev, AUDIO_FIFO_STATUS2);
	if (fifo_status2 & (AFIFO_UNDERFLOW_ST | AFIFO_OVERFLOW_ST)) {
		dev_warn(hdmirx_dev->dev, "%s: audio %s %#x, with fs %svalid %d\n",
			 __func__, audio_fifo_err(fifo_status2), fifo_status2,
			 is_validfs(fs_audio) ? "" : "in", fs_audio);
		if (is_validfs(fs_audio)) {
			hdmirx_audio_set_fs(hdmirx_dev, fs_audio);
			hdmirx_audio_set_ch(hdmirx_dev, ch_audio);
			hdmirx_enable_audio_output(hdmirx_dev, ch_audio, fs_audio, 0);
		}
		hdmirx_audio_fifo_init(hdmirx_dev);
		as->pre_state = 0;
		goto exit;
	}
	cur_state = fifo_status2 & 0xFFFF;
	init_state = as->init_state;
	pre_state = as->pre_state;
	dev_dbg(hdmirx_dev->dev, "%s: HDMI_RX_AUD_FIFO_FILLSTS1:%#x, single offset:%d, total offset:%d\n",
		__func__, cur_state, cur_state - pre_state, cur_state - init_state);
	if (!is_validfs(fs_audio)) {
		v4l2_dbg(1, debug, &hdmirx_dev->v4l2_dev,
			 "%s: no supported fs(%u), cur_state %d\n",
			 __func__, fs_audio, cur_state);
		delay = 1000;
	} else if (abs(fs_audio - as->fs_audio) > 1000 || ch_audio != as->ch_audio) {
		dev_info(hdmirx_dev->dev, "%s: restart audio fs(%d -> %d) ch(%d -> %d)\n",
			 __func__, as->fs_audio, fs_audio, as->ch_audio, ch_audio);
		hdmirx_audio_set_fs(hdmirx_dev, fs_audio);
		hdmirx_audio_set_ch(hdmirx_dev, ch_audio);
		hdmirx_enable_audio_output(hdmirx_dev, ch_audio, fs_audio, 0);
		hdmirx_audio_fifo_init(hdmirx_dev);
		as->pre_state = 0;
		goto exit;
	}

	if (cur_state != 0) {
		if (!hdmirx_dev->audio_present) {
			dev_info(hdmirx_dev->dev, "audio on");
			hdmirx_dev->audio_present = true;
		}
		if (cur_state - init_state > 16 && cur_state - pre_state > 0)
			hdmirx_audio_clk_ppm_inc(hdmirx_dev, 10);
		else if (cur_state - init_state < -16 && cur_state - pre_state < 0)
			hdmirx_audio_clk_ppm_inc(hdmirx_dev, -10);
	} else {
		if (hdmirx_dev->audio_present) {
			dev_info(hdmirx_dev->dev, "audio off");
			hdmirx_dev->audio_present = false;
		}
	}
	as->pre_state = cur_state;
exit:
	schedule_delayed_work_on(hdmirx_dev->bound_cpu,
			&hdmirx_dev->delayed_work_audio,
			msecs_to_jiffies(delay));
}

static void hdmirx_delayed_work_res_change(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk_hdmirx_dev *hdmirx_dev = container_of(dwork,
			struct rk_hdmirx_dev, delayed_work_res_change);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;
	bool plugin;
	int ret;

	mutex_lock(&hdmirx_dev->work_lock);
	plugin = tx_5v_power_present(hdmirx_dev);
	v4l2_dbg(1, debug, v4l2_dev, "%s: plugin:%d\n", __func__, plugin);
	if (plugin) {
		hdmirx_interrupts_setup(hdmirx_dev, false);
		hdmirx_submodule_init(hdmirx_dev);
		hdmirx_update_bits(hdmirx_dev, SCDC_CONFIG, POWERPROVIDED,
					POWERPROVIDED);
		hdmirx_hpd_ctrl(hdmirx_dev, true);
		hdmirx_phy_config(hdmirx_dev);
		hdmirx_audio_setup(hdmirx_dev);
		ret = hdmirx_wait_lock_and_get_timing(hdmirx_dev);
		if (ret) {
			hdmirx_plugout(hdmirx_dev);
			schedule_delayed_work_on(hdmirx_dev->bound_cpu,
						 &hdmirx_dev->delayed_work_hotplug,
						 msecs_to_jiffies(200));
		} else {
			hdmirx_dma_config(hdmirx_dev);
			hdmirx_interrupts_setup(hdmirx_dev, true);
			hdmirx_audio_handle_plugged_change(hdmirx_dev, 1);
		}
	}
	mutex_unlock(&hdmirx_dev->work_lock);
}

static void hdmirx_delayed_work_heartbeat(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk_hdmirx_dev *hdmirx_dev = container_of(dwork,
			struct rk_hdmirx_dev, delayed_work_heartbeat);

	queue_work_on(hdmirx_dev->wdt_cfg_bound_cpu,  system_highpri_wq,
			&hdmirx_dev->work_wdt_config);
	schedule_delayed_work_on(hdmirx_dev->bound_cpu,
			&hdmirx_dev->delayed_work_heartbeat, HZ);
}

static void hdmirx_work_wdt_config(struct work_struct *work)
{
	struct rk_hdmirx_dev *hdmirx_dev = container_of(work,
			struct rk_hdmirx_dev, work_wdt_config);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	sip_wdt_config(WDT_PING, 0, 0, 0);
	v4l2_dbg(3, debug, v4l2_dev, "hb\n");
}

static irqreturn_t hdmirx_5v_det_irq_handler(int irq, void *dev_id)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_id;
	u32 val;

	val = gpiod_get_value(hdmirx_dev->hdmirx_det_gpio);
	v4l2_dbg(3, debug, &hdmirx_dev->v4l2_dev, "%s: 5v:%d\n", __func__, val);

	schedule_delayed_work_on(hdmirx_dev->bound_cpu,
			&hdmirx_dev->delayed_work_hotplug, msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static const struct hdmirx_cec_ops hdmirx_cec_ops = {
	.write = hdmirx_writel,
	.read = hdmirx_readl,
};

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

	hdmirx_dev->rst_a = devm_reset_control_get(hdmirx_dev->dev, "rst_a");
	if (IS_ERR(hdmirx_dev->rst_a)) {
		dev_err(dev, "failed to get rst_a control\n");
		return PTR_ERR(hdmirx_dev->rst_a);
	}

	hdmirx_dev->rst_p = devm_reset_control_get(hdmirx_dev->dev, "rst_p");
	if (IS_ERR(hdmirx_dev->rst_p)) {
		dev_err(dev, "failed to get rst_p control\n");
		return PTR_ERR(hdmirx_dev->rst_p);
	}

	hdmirx_dev->rst_ref = devm_reset_control_get(hdmirx_dev->dev, "rst_ref");
	if (IS_ERR(hdmirx_dev->rst_ref)) {
		dev_err(dev, "failed to get rst_ref control\n");
		return PTR_ERR(hdmirx_dev->rst_ref);
	}

	hdmirx_dev->rst_biu = devm_reset_control_get(hdmirx_dev->dev, "rst_biu");
	if (IS_ERR(hdmirx_dev->rst_biu)) {
		dev_err(dev, "failed to get rst_biu control\n");
		return PTR_ERR(hdmirx_dev->rst_biu);
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

	if (of_property_read_bool(np, "hdcp1x-enable"))
		hdmirx_dev->hdcp1x_enable = true;

	ret = of_reserved_mem_device_init(dev);
	if (ret)
		dev_warn(dev, "No reserved memory for HDMIRX, use default CMA\n");

	return 0;
}

static void hdmirx_disable_all_interrupts(struct rk_hdmirx_dev *hdmirx_dev)
{
	hdmirx_audio_interrupts_setup(hdmirx_dev, false);
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

	hdmirx_update_bits(hdmirx_dev, PHY_CONFIG, PHY_RESET | PHY_PDDQ, 0);
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
	if (hdmirx_dev->edid_version == HDMIRX_EDID_600M)
		def_edid.edid = edid_init_data_600M;
	else
		def_edid.edid = edid_init_data_340M;
	ret = hdmirx_write_edid(hdmirx_dev, &def_edid, false);
	if (ret)
		dev_err(hdmirx_dev->dev, "%s write edid failed!\n", __func__);
}

static int hdmirx_runtime_suspend(struct device *dev)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);
	struct v4l2_device *v4l2_dev = &hdmirx_dev->v4l2_dev;

	disable_irq(hdmirx_dev->hdmi_irq);
	disable_irq(hdmirx_dev->dma_irq);
	sip_fiq_control(RK_SIP_FIQ_CTRL_FIQ_DIS, RK_IRQ_HDMIRX_HDMI, 0);

	cancel_delayed_work_sync(&hdmirx_dev->delayed_work_hotplug);
	cancel_delayed_work_sync(&hdmirx_dev->delayed_work_res_change);
	cancel_delayed_work_sync(&hdmirx_dev->delayed_work_audio);
	cancel_delayed_work_sync(&hdmirx_dev->delayed_work_heartbeat);
	flush_work(&hdmirx_dev->work_wdt_config);
	sip_wdt_config(WDT_STOP, 0, 0, 0);

	clk_bulk_disable_unprepare(hdmirx_dev->num_clks, hdmirx_dev->clks);

	v4l2_dbg(2, debug, v4l2_dev, "%s: suspend!\n", __func__);

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

	reset_control_assert(hdmirx_dev->rst_a);
	reset_control_assert(hdmirx_dev->rst_p);
	reset_control_assert(hdmirx_dev->rst_ref);
	reset_control_assert(hdmirx_dev->rst_biu);
	usleep_range(150, 160);
	reset_control_deassert(hdmirx_dev->rst_a);
	reset_control_deassert(hdmirx_dev->rst_p);
	reset_control_deassert(hdmirx_dev->rst_ref);
	reset_control_deassert(hdmirx_dev->rst_biu);
	usleep_range(150, 160);

	hdmirx_edid_init_config(hdmirx_dev);

	if (hdmirx_dev->initialized) {
		enable_irq(hdmirx_dev->hdmi_irq);
		enable_irq(hdmirx_dev->dma_irq);
		sip_fiq_control(RK_SIP_FIQ_CTRL_FIQ_EN, RK_IRQ_HDMIRX_HDMI, 0);
	}

	regmap_write(hdmirx_dev->vo1_grf, VO1_GRF_VO1_CON2,
		     (HDCP1_GATING_EN | HDMIRX_SDAIN_MSK | HDMIRX_SCLIN_MSK) |
		     ((HDCP1_GATING_EN | HDMIRX_SDAIN_MSK | HDMIRX_SCLIN_MSK) << 16));
	if (hdmirx_dev->initialized)
		schedule_delayed_work_on(hdmirx_dev->bound_cpu,
				&hdmirx_dev->delayed_work_hotplug,
				msecs_to_jiffies(20));
	else
		schedule_delayed_work_on(hdmirx_dev->bound_cpu,
				&hdmirx_dev->delayed_work_hotplug,
				msecs_to_jiffies(2000));

	return 0;
}

static const struct dev_pm_ops rk_hdmirx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(hdmirx_runtime_suspend, hdmirx_runtime_resume, NULL)
};

static ssize_t audio_rate_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d", hdmirx_dev->audio_state.fs_audio);
}

static ssize_t audio_present_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d",
			tx_5v_power_present(hdmirx_dev) ? hdmirx_dev->audio_present : 0);
}

static ssize_t edid_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);
	int edid = 0;

	if (hdmirx_dev)
		edid = hdmirx_dev->edid_version;

	return snprintf(buf, PAGE_SIZE, "%d\n", edid);
}

static ssize_t edid_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	int edid;
	struct rk_hdmirx_dev *hdmirx_dev = dev_get_drvdata(dev);

	if (!hdmirx_dev)
		return -EINVAL;

	if (kstrtoint(buf, 10, &edid))
		return -EINVAL;

	if (edid != HDMIRX_EDID_340M && edid != HDMIRX_EDID_600M)
		return count;

	if (hdmirx_dev->edid_version != edid) {
		disable_irq(hdmirx_dev->hdmi_irq);
		disable_irq(hdmirx_dev->dma_irq);
		sip_fiq_control(RK_SIP_FIQ_CTRL_FIQ_DIS, RK_IRQ_HDMIRX_HDMI, 0);

		if (tx_5v_power_present(hdmirx_dev))
			hdmirx_plugout(hdmirx_dev);
		hdmirx_dev->edid_version = edid;
		hdmirx_edid_init_config(hdmirx_dev);

		enable_irq(hdmirx_dev->hdmi_irq);
		enable_irq(hdmirx_dev->dma_irq);
		sip_fiq_control(RK_SIP_FIQ_CTRL_FIQ_EN, RK_IRQ_HDMIRX_HDMI, 0);
		schedule_delayed_work_on(hdmirx_dev->bound_cpu,
					 &hdmirx_dev->delayed_work_hotplug,
					 msecs_to_jiffies(500));
	}

	return count;
}

static DEVICE_ATTR_RO(audio_rate);
static DEVICE_ATTR_RO(audio_present);
static DEVICE_ATTR_RW(edid);

static struct attribute *hdmirx_attrs[] = {
	&dev_attr_audio_rate.attr,
	&dev_attr_audio_present.attr,
	&dev_attr_edid.attr,
	NULL
};
ATTRIBUTE_GROUPS(hdmirx);

static const struct hdmirx_reg_table hdmirx_ctrl_table[] = {
	{0x00, 0x0c, HDMIRX_ATTR_RO},
	{0x10, 0x10, HDMIRX_ATTR_RE},
	{0x14, 0x1c, HDMIRX_ATTR_RO},
	{0x20, 0x20, HDMIRX_ATTR_WO},
	{0x24, 0x28, HDMIRX_ATTR_RW},
	{0x40, 0x40, HDMIRX_ATTR_WO},
	{0x44, 0x44, HDMIRX_ATTR_RO},
	{0x48, 0x48, HDMIRX_ATTR_RW},
	{0x50, 0x50, HDMIRX_ATTR_RW},
	{0x60, 0x60, HDMIRX_ATTR_RW},
	{0x70, 0x70, HDMIRX_ATTR_RE},
	{0x74, 0x74, HDMIRX_ATTR_RW},
	{0x78, 0x78, HDMIRX_ATTR_RE},
	{0x7c, 0x7c, HDMIRX_ATTR_RO},
	{0x80, 0x84, HDMIRX_ATTR_RO},
	{0xc0, 0xc0, HDMIRX_ATTR_RO},
	{0xc4, 0xc4, HDMIRX_ATTR_RE},
	{0xc8, 0xc8, HDMIRX_ATTR_RO},
	{0xcc, 0xd4, HDMIRX_ATTR_RW},
	{0xd8, 0xd8, HDMIRX_ATTR_RO},
	{0xe0, 0xe8, HDMIRX_ATTR_RW},
	{0xf0, 0xf0, HDMIRX_ATTR_WO},
	{0xf4, 0xf4, HDMIRX_ATTR_RO},
	{0xf8, 0xf8, HDMIRX_ATTR_RW},
	{0x150, 0x150, HDMIRX_ATTR_RO},
	{0x160, 0x164, HDMIRX_ATTR_RW},
	{0x210, 0x214, HDMIRX_ATTR_RW},
	{0x218, 0x218, HDMIRX_ATTR_RO},
	{0x220, 0x228, HDMIRX_ATTR_RE},
	{0x22c, 0x22c, HDMIRX_ATTR_RW},
	{0x230, 0x230, HDMIRX_ATTR_WO},
	{0x234, 0x234, HDMIRX_ATTR_RO},
	{0x270, 0x274, HDMIRX_ATTR_RW},
	{0x278, 0x278, HDMIRX_ATTR_WO},
	{0x27c, 0x27c, HDMIRX_ATTR_RO},
	{0x290, 0x294, HDMIRX_ATTR_RW},
	{0x2a0, 0x2a8, HDMIRX_ATTR_WO},
	{0x2ac, 0x2ac, HDMIRX_ATTR_RO},
	{0x2b0, 0x2b4, HDMIRX_ATTR_RE},
	{0x2b8, 0x2b8, HDMIRX_ATTR_RO},
	{0x2bc, 0x2bc, HDMIRX_ATTR_RW},
	{0x2c0, 0x2d0, HDMIRX_ATTR_RO},
	{0x2d4, 0x2d8, HDMIRX_ATTR_RW},
	{0x2e0, 0x2e0, HDMIRX_ATTR_RW},
	{0x2e4, 0x2e4, HDMIRX_ATTR_RO},
	{0x2f0, 0x2f0, HDMIRX_ATTR_RW},
	{0x2f4, 0x2f4, HDMIRX_ATTR_RO},
	{0x2f8, 0x2f8, HDMIRX_ATTR_RW},
	{0x2fc, 0x2fc, HDMIRX_ATTR_RO},
	{0x300, 0x300, HDMIRX_ATTR_RW},
	{0x304, 0x304, HDMIRX_ATTR_RO},
	/* {0x3f0, 0x410, HDMIRX_ATTR_WO}, */
	{0x420, 0x434, HDMIRX_ATTR_RW},
	{0x460, 0x460, HDMIRX_ATTR_RW},
	/* {0x464, 0x478, HDMIRX_ATTR_WO}, */
	{0x480, 0x48c, HDMIRX_ATTR_RW},
	{0x490, 0x494, HDMIRX_ATTR_RO},
	{0x580, 0x580, HDMIRX_ATTR_RW},
	{0x584, 0x584, HDMIRX_ATTR_WO},
	{0x588, 0x59c, HDMIRX_ATTR_RO},
	{0x5a0, 0x5a4, HDMIRX_ATTR_RE},
	{0x5a8, 0x5bc, HDMIRX_ATTR_RO},
	{0x5c0, 0x5e0, HDMIRX_ATTR_RW},
	{0x700, 0x728, HDMIRX_ATTR_RW},
	{0x740, 0x74c, HDMIRX_ATTR_RW},
	{0x760, 0x760, HDMIRX_ATTR_RW},
	{0x764, 0x764, HDMIRX_ATTR_RO},
	{0x768, 0x768, HDMIRX_ATTR_RW},
	{0x7c0, 0x7c8, HDMIRX_ATTR_RW},
	{0x7cc, 0x7d0, HDMIRX_ATTR_RO},
	{0x7d4, 0x7d4, HDMIRX_ATTR_RW},
	{0x1580, 0x1598, HDMIRX_ATTR_RO},
	{0x2000, 0x2000, HDMIRX_ATTR_WO},
	{0x2004, 0x2004, HDMIRX_ATTR_RO},
	{0x2008, 0x200c, HDMIRX_ATTR_RW},
	{0x2020, 0x2030, HDMIRX_ATTR_RW},
	{0x2040, 0x2050, HDMIRX_ATTR_RO},
	{0x2060, 0x2068, HDMIRX_ATTR_RW},
	{0x4400, 0x4428, HDMIRX_ATTR_RW},
	{0x4430, 0x446c, HDMIRX_ATTR_RO},
	{0x5000, 0x5000, HDMIRX_ATTR_RO},
	{0x5010, 0x5010, HDMIRX_ATTR_RO},
	{0x5014, 0x5014, HDMIRX_ATTR_RW},
	{0x5020, 0x5020, HDMIRX_ATTR_RO},
	{0x5024, 0x5024, HDMIRX_ATTR_RW},
	{0x5030, 0x5030, HDMIRX_ATTR_RO},
	{0x5034, 0x5034, HDMIRX_ATTR_RW},
	{0x5040, 0x5040, HDMIRX_ATTR_RO},
	{0x5044, 0x5044, HDMIRX_ATTR_RW},
	{0x5050, 0x5050, HDMIRX_ATTR_RO},
	{0x5054, 0x5054, HDMIRX_ATTR_RW},
	{0x5080, 0x5080, HDMIRX_ATTR_RO},
	{0x5084, 0x5084, HDMIRX_ATTR_RW},
	{0x5090, 0x5090, HDMIRX_ATTR_RO},
	{0x5094, 0x5094, HDMIRX_ATTR_RW},
	{0x50a0, 0x50a0, HDMIRX_ATTR_RO},
	{0x50a4, 0x50a4, HDMIRX_ATTR_RW},
	{0x50c0, 0x50c0, HDMIRX_ATTR_RO},
	{0x50c4, 0x50c4, HDMIRX_ATTR_RW},
	{0x50d0, 0x50d0, HDMIRX_ATTR_RO},
	{0x50d4, 0x50d4, HDMIRX_ATTR_RW},
	{0x50e0, 0x50e0, HDMIRX_ATTR_RO},
	{0x50e4, 0x50e4, HDMIRX_ATTR_RW},
	{0x5100, 0x5100, HDMIRX_ATTR_RO},
	{0x5104, 0x5104, HDMIRX_ATTR_RW},
};

static int hdmirx_ctrl_show(struct seq_file *s, void *v)
{
	struct rk_hdmirx_dev *hdmirx_dev = s->private;
	u32 i = 0, j = 0, val = 0;

	seq_puts(s, "\n--------------------hdmirx ctrl--------------------");
	for (i = 0; i < ARRAY_SIZE(hdmirx_ctrl_table); i++) {
		for (j = hdmirx_ctrl_table[i].reg_base;
			j <= hdmirx_ctrl_table[i].reg_end; j += 4) {
			if (j % 16 == 0)
				seq_printf(s, "\n%08x:", j);
			if (hdmirx_ctrl_table[i].attr == HDMIRX_ATTR_WO)
				seq_puts(s, " WO......");
			else if (hdmirx_ctrl_table[i].attr == HDMIRX_ATTR_RE)
				seq_puts(s, " Reserved");
			else {
				val = hdmirx_readl(hdmirx_dev, j);
				seq_printf(s, " %08x", val);
			}
		}
	}
	seq_puts(s, "\n---------------------------------------------------\n");

	return 0;
}

static int hdmirx_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdmirx_ctrl_show, inode->i_private);
}

static ssize_t
hdmirx_ctrl_write(struct file *file, const char __user *buf,
		  size_t count, loff_t *ppos)
{
	struct rk_hdmirx_dev *hdmirx_dev =
		((struct seq_file *)file->private_data)->private;
	u32 reg, val;
	char kbuf[25];
	u32 i;
	bool write_en = false;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	if (sscanf(kbuf, "%x%x", &reg, &val) == -1)
		return -EFAULT;
	for (i = 0; i < ARRAY_SIZE(hdmirx_ctrl_table); i++) {
		if (reg >= hdmirx_ctrl_table[i].reg_base &&
		    reg <= hdmirx_ctrl_table[i].reg_end &&
		    (hdmirx_ctrl_table[i].attr == HDMIRX_ATTR_RW ||
		     hdmirx_ctrl_table[i].attr == HDMIRX_ATTR_WO)) {
			write_en = true;
			break;
		}
	}
	if (!write_en)
		return count;

	dev_info(hdmirx_dev->dev, "/**********hdmi register config******/");
	dev_info(hdmirx_dev->dev, "\n reg=%x val=%x\n", reg, val);
	hdmirx_writel(hdmirx_dev, reg, val);
	return count;
}

static const struct file_operations hdmirx_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = hdmirx_ctrl_open,
	.read = seq_read,
	.write = hdmirx_ctrl_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int hdmirx_phy_show(struct seq_file *s, void *v)
{
	struct rk_hdmirx_dev *hdmirx_dev = s->private;
	u32 i = 0, val = 0;

	seq_puts(s, "\n--------------------hdmirx phy---------------------\n");
	hdmirx_phy_register_read(hdmirx_dev, SUP_DIG_ANA_CREGS_SUP_ANA_NC, &val);
	seq_printf(s, "%08x: %08x\n", SUP_DIG_ANA_CREGS_SUP_ANA_NC, val);

	for (i = LANE0_DIG_ASIC_RX_OVRD_OUT_0;
	     i <= LANE3_DIG_ASIC_RX_OVRD_OUT_0; i += 0x100) {
		hdmirx_phy_register_read(hdmirx_dev, i, &val);
		seq_printf(s, "%08x: %08x\n", i, val);
	}
	for (i = LANE0_DIG_RX_VCOCAL_RX_VCO_CAL_CTRL_2;
	     i <= LANE3_DIG_RX_VCOCAL_RX_VCO_CAL_CTRL_2; i += 0x100) {
		hdmirx_phy_register_read(hdmirx_dev, i, &val);
		seq_printf(s, "%08x: %08x\n", i, val);
	}

	hdmirx_phy_register_read(hdmirx_dev,
		HDMIPCS_DIG_CTRL_PATH_MAIN_FSM_FSM_CONFIG, &val);
	seq_printf(s, "%08x: %08x\n",
		   HDMIPCS_DIG_CTRL_PATH_MAIN_FSM_FSM_CONFIG, val);
	hdmirx_phy_register_read(hdmirx_dev,
		HDMIPCS_DIG_CTRL_PATH_MAIN_FSM_ADAPT_REF_FOM, &val);
	seq_printf(s, "%08x: %08x\n",
		   HDMIPCS_DIG_CTRL_PATH_MAIN_FSM_ADAPT_REF_FOM, val);

	for (i = RAWLANE0_DIG_PCS_XF_RX_OVRD_OUT;
	     i <= RAWLANE3_DIG_PCS_XF_RX_OVRD_OUT; i += 0x100) {
		hdmirx_phy_register_read(hdmirx_dev, i, &val);
		seq_printf(s, "%08x: %08x\n", i, val);
	}
	for (i = RAWLANE0_DIG_AON_FAST_FLAGS;
	     i <= RAWLANE3_DIG_AON_FAST_FLAGS; i += 0x100) {
		hdmirx_phy_register_read(hdmirx_dev, i, &val);
		seq_printf(s, "%08x: %08x\n", i, val);
	}
	seq_puts(s, "---------------------------------------------------\n");

	return 0;
}

static int hdmirx_phy_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdmirx_phy_show, inode->i_private);
}

static ssize_t
hdmirx_phy_write(struct file *file, const char __user *buf,
		 size_t count, loff_t *ppos)
{
	struct rk_hdmirx_dev *hdmirx_dev =
		((struct seq_file *)file->private_data)->private;
	u32 reg, val;
	char kbuf[25];

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	if (sscanf(kbuf, "%x%x", &reg, &val) == -1)
		return -EFAULT;
	if (reg > RAWLANE3_DIG_AON_FAST_FLAGS) {
		dev_err(hdmirx_dev->dev, "it is no a hdmirx register\n");
		return count;
	}
	dev_info(hdmirx_dev->dev, "/**********hdmi register config******/");
	dev_info(hdmirx_dev->dev, "\n reg=%x val=%x\n", reg, val);
	hdmirx_phy_register_write(hdmirx_dev, reg, val);
	return count;
}

static const struct file_operations hdmirx_phy_fops = {
	.owner = THIS_MODULE,
	.open = hdmirx_phy_open,
	.read = seq_read,
	.write = hdmirx_phy_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int hdmirx_status_show(struct seq_file *s, void *v)
{
	struct rk_hdmirx_dev *hdmirx_dev = s->private;
	struct v4l2_dv_timings timings = hdmirx_dev->timings;
	struct v4l2_bt_timings *bt = &timings.bt;
	bool plugin;
	u32 htot, vtot, fps;
	u32 val;

	plugin = tx_5v_power_present(hdmirx_dev);
	seq_printf(s, "status: %s\n",  plugin ? "plugin" : "plugout");
	if (!plugin)
		return 0;

	val = hdmirx_readl(hdmirx_dev, SCDC_REGBANK_STATUS3);
	seq_puts(s, "Clk-Ch:");
	if (val & 0x1)
		seq_puts(s, "Lock\t");
	else
		seq_puts(s, "Unlock\t");
	seq_puts(s, "Ch0:");
	if (val & 0x2)
		seq_puts(s, "Lock\t");
	else
		seq_puts(s, "Unlock\t");
	seq_puts(s, "Ch1:");
	if (val & 0x4)
		seq_puts(s, "Lock\t");
	else
		seq_puts(s, "Unlock\t");
	seq_puts(s, "Ch2:");
	if (val & 0x8)
		seq_puts(s, "Lock\n");
	else
		seq_puts(s, "Unlock\n");

	val = hdmirx_readl(hdmirx_dev, 0x598);
	if (val & 0x8000)
		seq_printf(s, "Ch0-Err:%d\t", (val & 0x7fff));
	if (val & 0x80000000)
		seq_printf(s, "Ch1-Err:%d\t", (val & 0x7fff0000) >> 16);
	val = hdmirx_readl(hdmirx_dev, 0x59c);
	if (val & 0x8000)
		seq_printf(s, "Ch2-Err:%d", (val & 0x7fff));
	seq_puts(s, "\n");

	htot = bt->width + bt->hfrontporch + bt->hsync + bt->hbackporch;
	vtot = bt->height + bt->vfrontporch + bt->vsync + bt->vbackporch;
	if (bt->interlaced)
		vtot /= 2;

	fps = (bt->pixelclock + (htot * vtot) / 2) / (htot * vtot);
	if (hdmirx_dev->pix_fmt == HDMIRX_YUV420)
		fps *= 2;

	seq_puts(s, "Color Format: ");
	if (hdmirx_dev->pix_fmt == HDMIRX_RGB888)
		seq_puts(s, "RGB");
	else if (hdmirx_dev->pix_fmt == HDMIRX_YUV422)
		seq_puts(s, "YUV422");
	else if (hdmirx_dev->pix_fmt == HDMIRX_YUV444)
		seq_puts(s, "YUV444");
	else if (hdmirx_dev->pix_fmt == HDMIRX_YUV420)
		seq_puts(s, "YUV420");
	else
		seq_puts(s, "UNKNOWN");

	val = hdmirx_readl(hdmirx_dev, DMA_CONFIG1) & DDR_STORE_FORMAT_MASK;
	val = val >> 12;
	seq_puts(s, "\t\t\tStore Format: ");
	if (val == STORE_RGB888)
		seq_puts(s, "RGB\n");
	else if (val == STORE_RGBA_ARGB)
		seq_puts(s, "RGBA/ARGB\n");
	else if (val == STORE_YUV420_8BIT)
		seq_puts(s, "YUV420 (8 bit)\n");
	else if (val == STORE_YUV420_10BIT)
		seq_puts(s, "YUV420 (10 bit)\n");
	else if (val == STORE_YUV422_8BIT)
		seq_puts(s, "YUV422 (8 bit)\n");
	else if (val == STORE_YUV422_10BIT)
		seq_puts(s, "YUV422 (10 bit)\n");
	else if (val == STORE_YUV444_8BIT)
		seq_puts(s, "YUV444 (8 bit)\n");
	else if (val == STORE_YUV420_16BIT)
		seq_puts(s, "YUV420 (16 bit)\n");
	else if (val == STORE_YUV422_16BIT)
		seq_puts(s, "YUV422 (16 bit)\n");
	else
		seq_puts(s, "UNKNOWN\n");

	seq_printf(s, "Mode: %ux%u%s%u (%ux%u)",
		   bt->width, bt->height, bt->interlaced ? "i" : "p",
		   fps, htot, vtot);

	seq_printf(s, "\t\thfp:%d  hs:%d  hbp:%d  vfp:%d  vs:%d  vbp:%d\n",
		   bt->hfrontporch, bt->hsync, bt->hbackporch,
		   bt->vfrontporch, bt->vsync, bt->vbackporch);
	seq_printf(s, "Pixel Clk: %llu\n", bt->pixelclock);

	return 0;
}

static int hdmirx_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdmirx_status_show, inode->i_private);
}

static const struct file_operations hdmirx_status_fops = {
	.owner = THIS_MODULE,
	.open = hdmirx_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void hdmirx_register_debugfs(struct device *dev,
				    struct rk_hdmirx_dev *hdmirx_dev)
{
	hdmirx_dev->debugfs_dir = debugfs_create_dir("hdmirx", NULL);
	if (IS_ERR(hdmirx_dev->debugfs_dir))
		return;

	debugfs_create_file("ctrl", 0600, hdmirx_dev->debugfs_dir,
			    hdmirx_dev, &hdmirx_ctrl_fops);
	debugfs_create_file("phy", 0600, hdmirx_dev->debugfs_dir,
			    hdmirx_dev, &hdmirx_phy_fops);
	debugfs_create_file("status", 0600, hdmirx_dev->debugfs_dir,
			    hdmirx_dev, &hdmirx_status_fops);
}

static int hdmirx_set_cpu_limit_freq(struct rk_hdmirx_dev *hdmirx_dev)
{
	int ret;

	if (hdmirx_dev->policy == NULL) {
		hdmirx_dev->policy = cpufreq_cpu_get(hdmirx_dev->bound_cpu);
		if (!hdmirx_dev->policy) {
			dev_err(hdmirx_dev->dev, "%s: cpu%d policy NULL\n",
					__func__, hdmirx_dev->bound_cpu);
			return -1;
		}

		ret = freq_qos_add_request(&hdmirx_dev->policy->constraints,
					   &hdmirx_dev->min_sta_freq_req,
					   FREQ_QOS_MIN,
					   FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			dev_err(hdmirx_dev->dev,
				"%s: failed to add sta freq constraint\n",
				__func__);
			freq_qos_remove_request(&hdmirx_dev->min_sta_freq_req);
			hdmirx_dev->policy = NULL;
			return -1;
		}
		hdmirx_dev->freq_qos_add = true;
	}

	if (hdmirx_dev->freq_qos_add)
		freq_qos_update_request(&hdmirx_dev->min_sta_freq_req,
					hdmirx_dev->cpu_freq_khz);
	else
		dev_err(hdmirx_dev->dev, "%s freq qos nod add\n", __func__);

	return 0;
}

static void hdmirx_cancel_cpu_limit_freq(struct rk_hdmirx_dev *hdmirx_dev)
{
	if (hdmirx_dev->freq_qos_add)
		freq_qos_update_request(&hdmirx_dev->min_sta_freq_req,
					FREQ_QOS_MIN_DEFAULT_VALUE);
	else
		dev_err(hdmirx_dev->dev, "%s freq qos nod add\n", __func__);
}

static int hdmirx_probe(struct platform_device *pdev)
{
	const struct v4l2_dv_timings timings_def = HDMIRX_DEFAULT_TIMING;
	struct device *dev = &pdev->dev;
	struct rk_hdmirx_dev *hdmirx_dev;
	struct hdmirx_stream *stream;
	struct v4l2_device *v4l2_dev;
	struct v4l2_ctrl_handler *hdl;
	struct resource *res;
	int ret, irq, cpu_aff;
	struct hdmirx_cec_data cec_data;
	struct cpumask cpumask;

	hdmirx_dev = devm_kzalloc(dev, sizeof(*hdmirx_dev), GFP_KERNEL);
	if (!hdmirx_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, hdmirx_dev);
	hdmirx_dev->dev = dev;
	hdmirx_dev->of_node = dev->of_node;
	hdmirx_dev->cpu_freq_khz = CPU_LIMIT_FREQ_KHZ;
	hdmirx_dev->edid_version = HDMIRX_EDID_340M;
	ret = hdmirx_parse_dt(hdmirx_dev);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdmirx_regs");
	hdmirx_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdmirx_dev->regs)) {
		dev_err(dev, "failed to remap regs resource\n");
		return PTR_ERR(hdmirx_dev->regs);
	}

	if (cpu_logical_map(0) == 0)
		cpu_aff = cpu_logical_map(4); // big cpu0
	else

		cpu_aff = cpu_logical_map(1); // big cpu1
	sip_fiq_control(RK_SIP_FIQ_CTRL_SET_AFF, RK_IRQ_HDMIRX_HDMI, cpu_aff);
	hdmirx_dev->bound_cpu = (cpu_aff >> 8) & 0xf;
	hdmirx_dev->wdt_cfg_bound_cpu = hdmirx_dev->bound_cpu + 1;
	dev_info(dev, "%s: cpu_aff:%#x, Bound_cpu:%d, wdt_cfg_bound_cpu:%d\n",
			__func__, cpu_aff,
			hdmirx_dev->bound_cpu,
			hdmirx_dev->wdt_cfg_bound_cpu);
	cpu_latency_qos_add_request(&hdmirx_dev->pm_qos, PM_QOS_DEFAULT_VALUE);

	mutex_init(&hdmirx_dev->stream_lock);
	mutex_init(&hdmirx_dev->work_lock);
	spin_lock_init(&hdmirx_dev->dma_rst_lock);
	INIT_WORK(&hdmirx_dev->work_wdt_config,
			hdmirx_work_wdt_config);
	INIT_DELAYED_WORK(&hdmirx_dev->delayed_work_hotplug,
			hdmirx_delayed_work_hotplug);
	INIT_DELAYED_WORK(&hdmirx_dev->delayed_work_res_change,
			hdmirx_delayed_work_res_change);
	INIT_DELAYED_WORK(&hdmirx_dev->delayed_work_audio,
			hdmirx_delayed_work_audio);
	INIT_DELAYED_WORK(&hdmirx_dev->delayed_work_heartbeat,
			hdmirx_delayed_work_heartbeat);
	hdmirx_dev->power_on = false;

	ret = hdmirx_power_on(hdmirx_dev);
	if (ret)
		goto err_work_queues;

	hdmirx_dev->cur_fmt_fourcc = V4L2_PIX_FMT_BGR24;
	hdmirx_dev->timings = timings_def;

	irq = platform_get_irq_byname(pdev, "hdmi");
	if (irq < 0) {
		dev_err(dev, "get hdmi irq failed!\n");
		ret = irq;
		goto err_work_queues;
	}

	cpumask_clear(&cpumask);
	cpumask_set_cpu(hdmirx_dev->bound_cpu, &cpumask);
	irq_set_affinity_hint(irq, &cpumask);
	hdmirx_dev->hdmi_irq = irq;
	ret = devm_request_irq(dev, irq, hdmirx_hdmi_irq_handler, 0,
			       RK_HDMIRX_DRVNAME"-hdmi", hdmirx_dev);
	if (ret) {
		dev_err(dev, "request hdmi irq thread failed! ret:%d\n", ret);
		goto err_work_queues;
	}

	irq = platform_get_irq_byname(pdev, "dma");
	if (irq < 0) {
		dev_err(dev, "get dma irq failed!\n");
		ret = irq;
		goto err_work_queues;
	}

	cpumask_clear(&cpumask);
	cpumask_set_cpu(hdmirx_dev->bound_cpu, &cpumask);
	irq_set_affinity_hint(irq, &cpumask);
	hdmirx_dev->dma_irq = irq;
	ret = devm_request_threaded_irq(dev, irq, NULL, hdmirx_dma_irq_handler,
			IRQF_ONESHOT, RK_HDMIRX_DRVNAME"-dma", hdmirx_dev);
	if (ret) {
		dev_err(dev, "request dma irq thread failed! ret:%d\n", ret);
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

	ret = hdmirx_register_audio_device(hdmirx_dev);
	if (ret) {
		dev_err(dev, "register audio_driver failed!\n");
		goto err_unreg_video_dev;
	}
	ret = devm_add_action_or_reset(dev, hdmirx_unregister_audio_device, hdmirx_dev);
	if (ret)
		goto err_unreg_video_dev;

	hdmirx_dev->classdev = device_create_with_groups(hdmirx_class,
							 dev, MKDEV(0, 0),
							 hdmirx_dev,
							 hdmirx_groups,
							 "hdmirx");
	if (IS_ERR(hdmirx_dev->classdev)) {
		ret = PTR_ERR(hdmirx_dev->classdev);
		goto err_unreg_video_dev;
	}
	ret = devm_add_action_or_reset(dev, hdmirx_unregister_class_device, hdmirx_dev);
	if (ret)
		goto err_unreg_video_dev;

	irq = gpiod_to_irq(hdmirx_dev->hdmirx_det_gpio);
	if (irq < 0) {
		dev_err(dev, "failed to get hdmirx-det gpio irq\n");
		ret = irq;
		goto err_unreg_video_dev;
	}

	cpumask_clear(&cpumask);
	cpumask_set_cpu(hdmirx_dev->bound_cpu, &cpumask);
	irq_set_affinity_hint(irq, &cpumask);
	hdmirx_dev->det_irq = irq;
	ret = devm_request_irq(dev, irq, hdmirx_5v_det_irq_handler,
			       IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			       RK_HDMIRX_DRVNAME"-5v", hdmirx_dev);
	if (ret) {
		dev_err(dev, "request hdmirx-det gpio irq thread failed! ret:%d\n", ret);
		goto err_unreg_video_dev;
	}

	hdmirx_dev->cec_notifier = cec_notifier_conn_register(dev, NULL, NULL);
	if (!hdmirx_dev->cec_notifier) {
		ret = -ENOMEM;
		goto err_hdl;
	}

	irq = platform_get_irq_byname(pdev, "cec");
	if (irq < 0) {
		dev_err(dev, "get hdmi cec irq failed!\n");
		cec_notifier_conn_unregister(hdmirx_dev->cec_notifier);
		ret = irq;
		goto err_hdl;
	}
	cpumask_clear(&cpumask);
	cpumask_set_cpu(hdmirx_dev->bound_cpu, &cpumask);
	irq_set_affinity_hint(irq, &cpumask);

	cec_data.hdmirx = hdmirx_dev;
	cec_data.dev = hdmirx_dev->dev;
	cec_data.ops = &hdmirx_cec_ops;
	cec_data.irq = irq;
	cec_data.edid = edid_init_data_340M;
	hdmirx_dev->cec = rk_hdmirx_cec_register(&cec_data);
	hdmirx_register_hdcp(dev, hdmirx_dev, hdmirx_dev->hdcp1x_enable);

	hdmirx_register_debugfs(hdmirx_dev->dev, hdmirx_dev);

	hdmirx_dev->initialized = true;
	dev_info(dev, "%s driver probe ok!\n", dev_name(dev));

	return 0;

err_unreg_video_dev:
	video_unregister_device(&hdmirx_dev->stream.vdev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&hdmirx_dev->v4l2_dev);
err_hdl:
	v4l2_ctrl_handler_free(&hdmirx_dev->hdl);
err_work_queues:
	cpu_latency_qos_remove_request(&hdmirx_dev->pm_qos);
	cancel_delayed_work(&hdmirx_dev->delayed_work_hotplug);
	cancel_delayed_work(&hdmirx_dev->delayed_work_res_change);
	cancel_delayed_work(&hdmirx_dev->delayed_work_audio);
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

	debugfs_remove_recursive(hdmirx_dev->debugfs_dir);

	cpu_latency_qos_remove_request(&hdmirx_dev->pm_qos);
	cancel_delayed_work(&hdmirx_dev->delayed_work_hotplug);
	cancel_delayed_work(&hdmirx_dev->delayed_work_res_change);
	cancel_delayed_work(&hdmirx_dev->delayed_work_audio);
	clk_bulk_disable_unprepare(hdmirx_dev->num_clks, hdmirx_dev->clks);
	reset_control_assert(hdmirx_dev->rst_a);
	reset_control_assert(hdmirx_dev->rst_p);
	reset_control_assert(hdmirx_dev->rst_ref);
	reset_control_assert(hdmirx_dev->rst_biu);

	if (hdmirx_dev->cec)
		rk_hdmirx_cec_unregister(hdmirx_dev->cec);
	if (hdmirx_dev->cec_notifier)
		cec_notifier_conn_unregister(hdmirx_dev->cec_notifier);

	if (hdmirx_dev->hdcp)
		rk_hdmirx_hdcp_unregister(hdmirx_dev->hdcp);

	video_unregister_device(&hdmirx_dev->stream.vdev);
	v4l2_ctrl_handler_free(&hdmirx_dev->hdl);
	v4l2_device_unregister(&hdmirx_dev->v4l2_dev);

	if (hdmirx_dev->power_on)
		pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	of_reserved_mem_device_release(dev);

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

static int __init hdmirx_init(void)
{
	hdmirx_class = class_create(THIS_MODULE, "hdmirx");
	if (IS_ERR(hdmirx_class))
		return PTR_ERR(hdmirx_class);
	return platform_driver_register(&hdmirx_driver);
}
module_init(hdmirx_init);

static void __exit hdmirx_exit(void)
{
	platform_driver_unregister(&hdmirx_driver);
	class_destroy(hdmirx_class);
}
module_exit(hdmirx_exit);

MODULE_DESCRIPTION("Rockchip HDMI Receiver Driver");
MODULE_AUTHOR("Dingxian Wen <shawn.wen@rock-chips.com>");
MODULE_LICENSE("GPL v2");
