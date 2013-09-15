/*
 * Samsung HDMI interface driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Jiun Yu, <jiun.yu@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#ifndef SAMSUMG_HDMI_H
#define SAMSUNG_HDMI_H

#ifdef CONFIG_VIDEO_EXYNOS_HDMI_DEBUG
#define DEBUG
#endif

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/switch.h>
#include <linux/fb.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>

#include <plat/tv-core.h>

#define INFOFRAME_CNT          2

/** IP version definitions */
#define is_ip_ver_5g_1	(pdata->ip_ver == IP_VER_TV_5G_1)
#define is_ip_ver_5g	is_ip_ver_5g_1
#define is_ip_ver_5a_0	(pdata->ip_ver == IP_VER_TV_5A_0)
#define is_ip_ver_5a_1	(pdata->ip_ver == IP_VER_TV_5A_1)
#define is_ip_ver_5a	(is_ip_ver_5a_0 || is_ip_ver_5a_1)

/* default preset configured on probe */
#define HDMI_DEFAULT_PRESET	V4L2_DV_720P60

#define HDMI_VSI_VERSION	0x01
#define HDMI_AVI_VERSION	0x02
#define HDMI_AUI_VERSION	0x01
#define HDMI_VSI_LENGTH		0x05
#define HDMI_AVI_LENGTH		0x0d
#define HDMI_AUI_LENGTH		0x0a

#define AVI_UNDERSCAN			(2 << 0)
#define AVI_ACTIVE_FORMAT_VALID		(1 << 4)
#define AVI_PIC_ASPECT_RATIO_4_3	(1 << 4)
#define AVI_PIC_ASPECT_RATIO_16_9	(2 << 4)
#define AVI_SAME_AS_PIC_ASPECT_RATIO	8
#define AVI_LIMITED_RANGE		(1 << 2)
#define AVI_FULL_RANGE			(2 << 2)
#define AVI_ITU709			(2 << 6)

/* HDMI audio configuration value */
#define DEFAULT_SAMPLE_RATE	44100
#define DEFAULT_BITS_PER_SAMPLE	16
#define DEFAULT_SAMPLE_SIZE	24

/* HDMI pad definitions */
#define HDMI_PAD_SINK		0
#define HDMI_PADS_NUM		1

/* HPD state definitions */
#define HPD_LOW		0
#define HPD_HIGH	1

enum HDMI_VIDEO_FORMAT {
	HDMI_VIDEO_FORMAT_2D = 0x0,
	/** refer to Table 8-12 HDMI_Video_Format in HDMI specification v1.4a */
	HDMI_VIDEO_FORMAT_3D = 0x2
};

enum HDMI_3D_FORMAT {
	/** refer to Table 8-13 3D_Structure in HDMI specification v1.4a */

	/** Frame Packing */
	HDMI_3D_FORMAT_FP = 0x0,
	/** Top-and-Bottom */
	HDMI_3D_FORMAT_TB = 0x6,
	/** Side-by-Side Half */
	HDMI_3D_FORMAT_SB_HALF = 0x8
};

enum HDMI_3D_EXT_DATA {
	/* refer to Table H-3 3D_Ext_Data - Additional video format
	 * information for Side-by-side(half) 3D structure */

	/** Horizontal sub-sampleing */
	HDMI_H_SUB_SAMPLE = 0x1
};

enum HDMI_OUTPUT_FMT {
	HDMI_OUTPUT_RGB888 = 0x0,
	HDMI_OUTPUT_YUV444 = 0x2
};

enum HDMI_PACKET_TYPE {
	/** refer to Table 5-8 Packet Type in HDMI specification v1.4a */

	/** InfoFrame packet type */
	HDMI_PACKET_TYPE_INFOFRAME = 0X80,
	/** Vendor-Specific InfoFrame */
	HDMI_PACKET_TYPE_VSI = HDMI_PACKET_TYPE_INFOFRAME + 1,
	/** Auxiliary Video information InfoFrame */
	HDMI_PACKET_TYPE_AVI = HDMI_PACKET_TYPE_INFOFRAME + 2,
	/** Audio information InfoFrame */
	HDMI_PACKET_TYPE_AUI = HDMI_PACKET_TYPE_INFOFRAME + 4
};

enum HDMI_COLOR_RANGE {
	HDMI_RGB601_0_255,
	HDMI_RGB601_16_235,
	HDMI_RGB709_0_255,
	HDMI_RGB709_16_235
};

enum HDMI_PROBE_STATE {
	HDMI_PROBED,
	HDMI_PROBING
};

enum HDMI_AUDIO_CODEC {
	HDMI_AUDIO_PCM,
	HDMI_AUDIO_AC3,
	HDMI_AUDIO_MP3
};

enum HDMI_ASPECT_RATIO {
	HDMI_ASPECT_RATIO_16_9,
	HDMI_ASPECT_RATIO_4_3
};

enum HDMI_STREAMING_STATE {
	HDMI_STOP		= 0 << 1,
	HDMI_STREAMING		= 1 << 1
};

enum HDCP_EVENT {
	HDCP_EVENT_STOP			= 1 << 0,
	HDCP_EVENT_START		= 1 << 1,
	HDCP_EVENT_READ_BKSV_START	= 1 << 2,
	HDCP_EVENT_WRITE_AKSV_START	= 1 << 4,
	HDCP_EVENT_CHECK_RI_START	= 1 << 8,
	HDCP_EVENT_SECOND_AUTH_START	= 1 << 16
};

enum HDCP_STATE {
	NOT_AUTHENTICATED,
	RECEIVER_READ_READY,
	BCAPS_READ_DONE,
	BKSV_READ_DONE,
	AN_WRITE_DONE,
	AKSV_WRITE_DONE,
	FIRST_AUTHENTICATION_DONE,
	SECOND_AUTHENTICATION_RDY,
	SECOND_AUTHENTICATION_DONE,
};

#define DEFAULT_AUDIO_CODEC	HDMI_AUDIO_PCM

struct hdmi_resources {
	struct clk *hdmi;
	struct clk *sclk_hdmi;
	struct clk *sclk_pixel;
	struct clk *sclk_hdmiphy;
	struct regulator_bulk_data *regul_bulk;
	int regul_count;
};

struct hdmi_tg_regs {
	u8 cmd;
	u8 h_fsz_l;
	u8 h_fsz_h;
	u8 hact_st_l;
	u8 hact_st_h;
	u8 hact_sz_l;
	u8 hact_sz_h;
	u8 v_fsz_l;
	u8 v_fsz_h;
	u8 vsync_l;
	u8 vsync_h;
	u8 vsync2_l;
	u8 vsync2_h;
	u8 vact_st_l;
	u8 vact_st_h;
	u8 vact_sz_l;
	u8 vact_sz_h;
	u8 field_chg_l;
	u8 field_chg_h;
	u8 vact_st2_l;
	u8 vact_st2_h;
#ifndef CONFIG_CPU_EXYNOS4210
	u8 vact_st3_l;
	u8 vact_st3_h;
	u8 vact_st4_l;
	u8 vact_st4_h;
#endif
	u8 vsync_top_hdmi_l;
	u8 vsync_top_hdmi_h;
	u8 vsync_bot_hdmi_l;
	u8 vsync_bot_hdmi_h;
	u8 field_top_hdmi_l;
	u8 field_top_hdmi_h;
	u8 field_bot_hdmi_l;
	u8 field_bot_hdmi_h;
#ifndef CONFIG_CPU_EXYNOS4210
	u8 tg_3d;
#endif
};

struct hdmi_core_regs {
#ifndef CONFIG_CPU_EXYNOS4210
	u8 h_blank[2];
	u8 v2_blank[2];
	u8 v1_blank[2];
	u8 v_line[2];
	u8 h_line[2];
	u8 hsync_pol[1];
	u8 vsync_pol[1];
	u8 int_pro_mode[1];
	u8 v_blank_f0[2];
	u8 v_blank_f1[2];
	u8 h_sync_start[2];
	u8 h_sync_end[2];
	u8 v_sync_line_bef_2[2];
	u8 v_sync_line_bef_1[2];
	u8 v_sync_line_aft_2[2];
	u8 v_sync_line_aft_1[2];
	u8 v_sync_line_aft_pxl_2[2];
	u8 v_sync_line_aft_pxl_1[2];
	u8 v_blank_f2[2]; /* for 3D mode */
	u8 v_blank_f3[2]; /* for 3D mode */
	u8 v_blank_f4[2]; /* for 3D mode */
	u8 v_blank_f5[2]; /* for 3D mode */
	u8 v_sync_line_aft_3[2];
	u8 v_sync_line_aft_4[2];
	u8 v_sync_line_aft_5[2];
	u8 v_sync_line_aft_6[2];
	u8 v_sync_line_aft_pxl_3[2];
	u8 v_sync_line_aft_pxl_4[2];
	u8 v_sync_line_aft_pxl_5[2];
	u8 v_sync_line_aft_pxl_6[2];
	u8 vact_space_1[2];
	u8 vact_space_2[2];
	u8 vact_space_3[2];
	u8 vact_space_4[2];
	u8 vact_space_5[2];
	u8 vact_space_6[2];
#else
	u8 h_blank[2];
	u8 v_blank[3];
	u8 h_v_line[3];
	u8 vsync_pol[1];
	u8 int_pro_mode[1];
	u8 v_blank_f[3];
	u8 h_sync_gen[3];
	u8 v_sync_gen1[3];
	u8 v_sync_gen2[3];
	u8 v_sync_gen3[3];
#endif
};

struct hdmi_3d_info {
	enum HDMI_VIDEO_FORMAT is_3d;
	enum HDMI_3D_FORMAT fmt_3d;
};

struct hdmi_preset_conf {
	struct hdmi_core_regs core;
	struct hdmi_tg_regs tg;
	struct v4l2_mbus_framefmt mbus_fmt;
	u8 vic;
};

struct hdmi_driver_data {
	int hdmiphy_bus;
};

struct hdmi_infoframe {
	enum HDMI_PACKET_TYPE type;
	u8 ver;
	u8 len;
};

struct hdcp_info {
	u8 is_repeater;
	u32 hdcp_start;
	int hdcp_enable;

	enum HDCP_EVENT	event;
	enum HDCP_STATE	auth_status;
};

struct hdmi_device {
	/** base address of HDMI registers */
	void __iomem *regs;
	void __iomem *phy_regs;

	/** HDMI interrupt */
	unsigned int int_irq;
	unsigned int ext_irq;

	/** pointer to device parent */
	struct device *dev;
	/** platform data of hdmi */
	struct s5p_hdmi_platdata *pdata;
	/** subdev generated by HDMI device */
	struct v4l2_subdev sd;
	/** sink pad connected to mixer */
	struct media_pad pad;
	/** V4L2 device structure */
	struct v4l2_device v4l2_dev;
	/** subdev of HDMIPHY interface */
	struct v4l2_subdev *phy_sd;
	/** configuration of current graphic mode */
	const struct hdmi_preset_conf *cur_conf;
	/** current preset */
	u32 cur_preset;
	/** other resources */
	struct hdmi_resources res;
	/** supported HDMI InfoFrame */
	struct hdmi_infoframe infoframe[INFOFRAME_CNT];
	/** audio on/off control flag */
	int audio_enable;
	/** number of audio channels */
	int audio_channel_count;
	/** audio sample rate */
	int sample_rate;
	/** audio bits per sample */
	int bits_per_sample;
	/** audio sample size */
	int sample_size;
	/** RGB Quantization range and Colorimetry */
	enum HDMI_COLOR_RANGE color_range;
	/** HDMI is streaming or not */
	enum HDMI_STREAMING_STATE streaming;
	/** current probe state of hdmi driver */
	enum HDMI_PROBE_STATE probe_state;
	/** current audio codec type */
	enum HDMI_AUDIO_CODEC audio_codec;
	/** HDMI output format */
	enum HDMI_OUTPUT_FMT output_fmt;
	/** Aspect ratio information */
	enum HDMI_ASPECT_RATIO aspect;

	/** HDCP information */
	struct hdcp_info hdcp_info;
	struct work_struct work;
	struct workqueue_struct	*hdcp_wq;

	/** delayed work queue for hdmiphy power off */
	struct delayed_work hdmi_probe_work;

	/* HPD releated */
	struct work_struct hpd_work;
	struct delayed_work hpd_work_ext;
	struct switch_dev hpd_switch;

	/* choose DVI or HDMI mode */
	int dvi_mode;

	/** mutex for protection of fields below */
	struct mutex mutex;
};

struct hdmi_conf {
	u32 preset;
	const struct hdmi_preset_conf *conf;
	const struct hdmi_3d_info *info;
};
extern const struct hdmi_conf hdmi_conf[];

struct hdmiphy_conf {
	u32 preset;
	const u8 *data;
};
extern const struct hdmiphy_conf hdmiphy_conf[];
extern const int hdmi_pre_cnt;
extern const int hdmiphy_conf_cnt;

const struct hdmi_3d_info *hdmi_preset2info(u32 preset);

irqreturn_t hdmi_irq_handler(int irq, void *dev_data);
const u8 *hdmiphy_preset2conf(u32 preset);
int hdmi_conf_apply(struct hdmi_device *hdmi_dev);
int is_hdmiphy_ready(struct hdmi_device *hdev);
void hdmi_enable(struct hdmi_device *hdev, int on);
void hdmi_hpd_enable(struct hdmi_device *hdev, int on);
void hdmi_hpd_clear_int(struct hdmi_device *hdev);
void hdmi_tg_enable(struct hdmi_device *hdev, int on);
void hdmi_reg_stop_vsi(struct hdmi_device *hdev);
void hdmi_reg_infoframe(struct hdmi_device *hdev,
		struct hdmi_infoframe *infoframe);
void hdmi_reg_set_limits(struct hdmi_device *hdev);
void hdmi_reg_set_acr(struct hdmi_device *hdev);
void hdmi_reg_spdif_audio_init(struct hdmi_device *hdev);
void hdmi_reg_i2s_audio_init(struct hdmi_device *hdev);
void hdmi_audio_enable(struct hdmi_device *hdev, int on);
void hdmi_bluescreen_enable(struct hdmi_device *hdev, int on);
void hdmi_reg_mute(struct hdmi_device *hdev, int on);
void hdmi_reg_set_int_hpd(struct hdmi_device *hdev);
void hdmi_reg_set_ext_hpd(struct hdmi_device *hdev);
int hdmi_hpd_status(struct hdmi_device *hdev);
int is_hdmi_streaming(struct hdmi_device *hdev);
u8 hdmi_get_int_mask(struct hdmi_device *hdev);
void hdmi_set_int_mask(struct hdmi_device *hdev, u8 mask, int en);
void hdmi_sw_hpd_enable(struct hdmi_device *hdev, int en);
void hdmi_sw_hpd_plug(struct hdmi_device *hdev, int en);
void hdmi_phy_sw_reset(struct hdmi_device *hdev);
void hdmi_sw_reset(struct hdmi_device *hdev);
void hdmi_dumpregs(struct hdmi_device *hdev, char *prefix);
void hdmi_set_3d_info(struct hdmi_device *hdev);
void hdmi_set_dvi_mode(struct hdmi_device *hdev);
void hdmi_debugfs_init(struct hdmi_device *hdev);

/** HDMI PHY setting for Exynos5410 */
void hdmiphy_set_power(struct hdmi_device *hdev, int en);
void hdmiphy_conf_store(struct hdmi_device *hdev, const char *buf);
void hdmiphy_set_mode(struct hdmi_device *hdev, int en);

/** HDCP functions */
irqreturn_t hdcp_irq_handler(struct hdmi_device *hdev);
int hdcp_stop(struct hdmi_device *hdev);
int hdcp_start(struct hdmi_device *hdev);
int hdcp_prepare(struct hdmi_device *hdev);
int hdcp_i2c_read(struct hdmi_device *hdev, u8 offset, int bytes, u8 *buf);
int hdcp_i2c_write(struct hdmi_device *hdev, u8 offset, int bytes, u8 *buf);

/** EDID functions */
int edid_update(struct hdmi_device *hdev);
u32 edid_enum_presets(struct hdmi_device *hdev, int index);
u32 edid_preferred_preset(struct hdmi_device *hdev);
bool edid_supports_hdmi(struct hdmi_device *hdev);
int edid_max_audio_channels(struct hdmi_device *hdev);
int edid_source_phy_addr(struct hdmi_device *hdev);

static inline
void hdmi_write(struct hdmi_device *hdev, u32 reg_id, u32 value)
{
	writel(value, hdev->regs + reg_id);
}

static inline
void hdmi_write_mask(struct hdmi_device *hdev, u32 reg_id, u32 value, u32 mask)
{
	u32 old = readl(hdev->regs + reg_id);
	value = (value & mask) | (old & ~mask);
	writel(value, hdev->regs + reg_id);
}

static inline
void hdmiphy_write_mask(struct hdmi_device *hdev, u32 reg_id, u32 value, u32 mask)
{
	u32 old = readl(hdev->phy_regs + reg_id);
	value = (value & mask) | (old & ~mask);
	writel(value, hdev->phy_regs + reg_id);
}

static inline
void hdmi_writeb(struct hdmi_device *hdev, u32 reg_id, u8 value)
{
	writeb(value, hdev->regs + reg_id);
}

static inline void hdmi_write_bytes(struct hdmi_device *hdev, u32 reg_id,
		u8 *buf, int bytes)
{
	int i;

	for (i = 0; i < bytes; ++i)
		writeb(buf[i], hdev->regs + reg_id + i * 4);
}

static inline u32 hdmi_read(struct hdmi_device *hdev, u32 reg_id)
{
	return readl(hdev->regs + reg_id);
}

static inline u8 hdmi_readb(struct hdmi_device *hdev, u32 reg_id)
{
	return readb(hdev->regs + reg_id);
}

static inline void hdmi_read_bytes(struct hdmi_device *hdev, u32 reg_id,
		u8 *buf, int bytes)
{
	int i;

	for (i = 0; i < bytes; ++i)
		buf[i] = readb(hdev->regs + reg_id + i * 4);
}

int hdmi_get_v4l2_dv_id(void);
int hdmi_get_phy_mode(void);

static struct edid_preset {
	u32 preset;
	u16 xres;
	u16 yres;
	u16 refresh;
	u32 vmode;
	char *name;
	bool supported;
} edid_presets[] = {
	{ V4L2_DV_480P59_94,	720,  480,  59, FB_VMODE_NONINTERLACED, "480p@59.94" },
	{ V4L2_DV_480P60,	720,  480,  60, FB_VMODE_NONINTERLACED, "480p@60" },
	{ V4L2_DV_576P50,	720,  576,  50, FB_VMODE_NONINTERLACED, "576p@50" },
	{ V4L2_DV_720P50,	1280, 720,  50, FB_VMODE_NONINTERLACED, "720p@50" },
	{ V4L2_DV_720P59_94,	1280, 720,  59, FB_VMODE_NONINTERLACED, "720p@59.94" },
	{ V4L2_DV_720P60,	1280, 720,  60, FB_VMODE_NONINTERLACED, "720p@60" },
	{ V4L2_DV_1080P24,	1920, 1080, 24, FB_VMODE_NONINTERLACED, "1080p@24" },
	{ V4L2_DV_1080P25,	1920, 1080, 25, FB_VMODE_NONINTERLACED, "1080p@25" },
	{ V4L2_DV_1080P30,	1920, 1080, 30, FB_VMODE_NONINTERLACED, "1080p@30" },
	{ V4L2_DV_1080P50,	1920, 1080, 50, FB_VMODE_NONINTERLACED, "1080p@50" },
	{ V4L2_DV_1080P59_94,	1920, 1080, 59, FB_VMODE_NONINTERLACED, "1080p@59.94" },
	{ V4L2_DV_1080P60,	1920, 1080, 60, FB_VMODE_NONINTERLACED, "1080p@60" },
	{ V4L2_DV_1080I50,	1920, 1080, 50, FB_VMODE_INTERLACED, "1080i@50" },
	{ V4L2_DV_1080I59_94,	1920, 1080, 59, FB_VMODE_INTERLACED, "1080i@59.94" },
	{ V4L2_DV_1080I60,	1920, 1080, 60, FB_VMODE_INTERLACED, "1080i@60" },
};

#endif /* SAMSUNG_HDMI_H */
