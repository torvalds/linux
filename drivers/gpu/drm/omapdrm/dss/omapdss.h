/*
 * Copyright (C) 2016 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OMAP_DRM_DSS_H
#define __OMAP_DRM_DSS_H

#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <video/videomode.h>
#include <linux/platform_data/omapdss.h>
#include <uapi/drm/drm_mode.h>

#define DISPC_IRQ_FRAMEDONE		(1 << 0)
#define DISPC_IRQ_VSYNC			(1 << 1)
#define DISPC_IRQ_EVSYNC_EVEN		(1 << 2)
#define DISPC_IRQ_EVSYNC_ODD		(1 << 3)
#define DISPC_IRQ_ACBIAS_COUNT_STAT	(1 << 4)
#define DISPC_IRQ_PROG_LINE_NUM		(1 << 5)
#define DISPC_IRQ_GFX_FIFO_UNDERFLOW	(1 << 6)
#define DISPC_IRQ_GFX_END_WIN		(1 << 7)
#define DISPC_IRQ_PAL_GAMMA_MASK	(1 << 8)
#define DISPC_IRQ_OCP_ERR		(1 << 9)
#define DISPC_IRQ_VID1_FIFO_UNDERFLOW	(1 << 10)
#define DISPC_IRQ_VID1_END_WIN		(1 << 11)
#define DISPC_IRQ_VID2_FIFO_UNDERFLOW	(1 << 12)
#define DISPC_IRQ_VID2_END_WIN		(1 << 13)
#define DISPC_IRQ_SYNC_LOST		(1 << 14)
#define DISPC_IRQ_SYNC_LOST_DIGIT	(1 << 15)
#define DISPC_IRQ_WAKEUP		(1 << 16)
#define DISPC_IRQ_SYNC_LOST2		(1 << 17)
#define DISPC_IRQ_VSYNC2		(1 << 18)
#define DISPC_IRQ_VID3_END_WIN		(1 << 19)
#define DISPC_IRQ_VID3_FIFO_UNDERFLOW	(1 << 20)
#define DISPC_IRQ_ACBIAS_COUNT_STAT2	(1 << 21)
#define DISPC_IRQ_FRAMEDONE2		(1 << 22)
#define DISPC_IRQ_FRAMEDONEWB		(1 << 23)
#define DISPC_IRQ_FRAMEDONETV		(1 << 24)
#define DISPC_IRQ_WBBUFFEROVERFLOW	(1 << 25)
#define DISPC_IRQ_WBUNCOMPLETEERROR	(1 << 26)
#define DISPC_IRQ_SYNC_LOST3		(1 << 27)
#define DISPC_IRQ_VSYNC3		(1 << 28)
#define DISPC_IRQ_ACBIAS_COUNT_STAT3	(1 << 29)
#define DISPC_IRQ_FRAMEDONE3		(1 << 30)

struct omap_dss_device;
struct omap_overlay_manager;
struct dss_lcd_mgr_config;
struct snd_aes_iec958;
struct snd_cea_861_aud_if;
struct hdmi_avi_infoframe;

enum omap_display_type {
	OMAP_DISPLAY_TYPE_NONE		= 0,
	OMAP_DISPLAY_TYPE_DPI		= 1 << 0,
	OMAP_DISPLAY_TYPE_DBI		= 1 << 1,
	OMAP_DISPLAY_TYPE_SDI		= 1 << 2,
	OMAP_DISPLAY_TYPE_DSI		= 1 << 3,
	OMAP_DISPLAY_TYPE_VENC		= 1 << 4,
	OMAP_DISPLAY_TYPE_HDMI		= 1 << 5,
	OMAP_DISPLAY_TYPE_DVI		= 1 << 6,
};

enum omap_plane_id {
	OMAP_DSS_GFX	= 0,
	OMAP_DSS_VIDEO1	= 1,
	OMAP_DSS_VIDEO2	= 2,
	OMAP_DSS_VIDEO3	= 3,
	OMAP_DSS_WB	= 4,
};

enum omap_channel {
	OMAP_DSS_CHANNEL_LCD	= 0,
	OMAP_DSS_CHANNEL_DIGIT	= 1,
	OMAP_DSS_CHANNEL_LCD2	= 2,
	OMAP_DSS_CHANNEL_LCD3	= 3,
	OMAP_DSS_CHANNEL_WB	= 4,
};

enum omap_color_mode {
	OMAP_DSS_COLOR_CLUT1	= 1 << 0,  /* BITMAP 1 */
	OMAP_DSS_COLOR_CLUT2	= 1 << 1,  /* BITMAP 2 */
	OMAP_DSS_COLOR_CLUT4	= 1 << 2,  /* BITMAP 4 */
	OMAP_DSS_COLOR_CLUT8	= 1 << 3,  /* BITMAP 8 */
	OMAP_DSS_COLOR_RGB12U	= 1 << 4,  /* RGB12, 16-bit container */
	OMAP_DSS_COLOR_ARGB16	= 1 << 5,  /* ARGB16 */
	OMAP_DSS_COLOR_RGB16	= 1 << 6,  /* RGB16 */
	OMAP_DSS_COLOR_RGB24U	= 1 << 7,  /* RGB24, 32-bit container */
	OMAP_DSS_COLOR_RGB24P	= 1 << 8,  /* RGB24, 24-bit container */
	OMAP_DSS_COLOR_YUV2	= 1 << 9,  /* YUV2 4:2:2 co-sited */
	OMAP_DSS_COLOR_UYVY	= 1 << 10, /* UYVY 4:2:2 co-sited */
	OMAP_DSS_COLOR_ARGB32	= 1 << 11, /* ARGB32 */
	OMAP_DSS_COLOR_RGBA32	= 1 << 12, /* RGBA32 */
	OMAP_DSS_COLOR_RGBX32	= 1 << 13, /* RGBx32 */
	OMAP_DSS_COLOR_NV12		= 1 << 14, /* NV12 format: YUV 4:2:0 */
	OMAP_DSS_COLOR_RGBA16		= 1 << 15, /* RGBA16 - 4444 */
	OMAP_DSS_COLOR_RGBX16		= 1 << 16, /* RGBx16 - 4444 */
	OMAP_DSS_COLOR_ARGB16_1555	= 1 << 17, /* ARGB16 - 1555 */
	OMAP_DSS_COLOR_XRGB16_1555	= 1 << 18, /* xRGB16 - 1555 */
};

enum omap_dss_load_mode {
	OMAP_DSS_LOAD_CLUT_AND_FRAME	= 0,
	OMAP_DSS_LOAD_CLUT_ONLY		= 1,
	OMAP_DSS_LOAD_FRAME_ONLY	= 2,
	OMAP_DSS_LOAD_CLUT_ONCE_FRAME	= 3,
};

enum omap_dss_trans_key_type {
	OMAP_DSS_COLOR_KEY_GFX_DST = 0,
	OMAP_DSS_COLOR_KEY_VID_SRC = 1,
};

enum omap_rfbi_te_mode {
	OMAP_DSS_RFBI_TE_MODE_1 = 1,
	OMAP_DSS_RFBI_TE_MODE_2 = 2,
};

enum omap_dss_signal_level {
	OMAPDSS_SIG_ACTIVE_LOW,
	OMAPDSS_SIG_ACTIVE_HIGH,
};

enum omap_dss_signal_edge {
	OMAPDSS_DRIVE_SIG_FALLING_EDGE,
	OMAPDSS_DRIVE_SIG_RISING_EDGE,
};

enum omap_dss_venc_type {
	OMAP_DSS_VENC_TYPE_COMPOSITE,
	OMAP_DSS_VENC_TYPE_SVIDEO,
};

enum omap_dss_dsi_pixel_format {
	OMAP_DSS_DSI_FMT_RGB888,
	OMAP_DSS_DSI_FMT_RGB666,
	OMAP_DSS_DSI_FMT_RGB666_PACKED,
	OMAP_DSS_DSI_FMT_RGB565,
};

enum omap_dss_dsi_mode {
	OMAP_DSS_DSI_CMD_MODE = 0,
	OMAP_DSS_DSI_VIDEO_MODE,
};

enum omap_display_caps {
	OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE	= 1 << 0,
	OMAP_DSS_DISPLAY_CAP_TEAR_ELIM		= 1 << 1,
};

enum omap_dss_display_state {
	OMAP_DSS_DISPLAY_DISABLED = 0,
	OMAP_DSS_DISPLAY_ACTIVE,
};

enum omap_dss_rotation_type {
	OMAP_DSS_ROT_DMA	= 1 << 0,
	OMAP_DSS_ROT_VRFB	= 1 << 1,
	OMAP_DSS_ROT_TILER	= 1 << 2,
};

/* clockwise rotation angle */
enum omap_dss_rotation_angle {
	OMAP_DSS_ROT_0   = 0,
	OMAP_DSS_ROT_90  = 1,
	OMAP_DSS_ROT_180 = 2,
	OMAP_DSS_ROT_270 = 3,
};

enum omap_overlay_caps {
	OMAP_DSS_OVL_CAP_SCALE = 1 << 0,
	OMAP_DSS_OVL_CAP_GLOBAL_ALPHA = 1 << 1,
	OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA = 1 << 2,
	OMAP_DSS_OVL_CAP_ZORDER = 1 << 3,
	OMAP_DSS_OVL_CAP_POS = 1 << 4,
	OMAP_DSS_OVL_CAP_REPLICATION = 1 << 5,
};

enum omap_overlay_manager_caps {
	OMAP_DSS_DUMMY_VALUE, /* add a dummy value to prevent compiler error */
};

enum omap_dss_clk_source {
	OMAP_DSS_CLK_SRC_FCK = 0,		/* OMAP2/3: DSS1_ALWON_FCLK
						 * OMAP4: DSS_FCLK */
	OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC,	/* OMAP3: DSI1_PLL_FCLK
						 * OMAP4: PLL1_CLK1 */
	OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DSI,	/* OMAP3: DSI2_PLL_FCLK
						 * OMAP4: PLL1_CLK2 */
	OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC,	/* OMAP4: PLL2_CLK1 */
	OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DSI,	/* OMAP4: PLL2_CLK2 */
};

enum omap_hdmi_flags {
	OMAP_HDMI_SDA_SCL_EXTERNAL_PULLUP = 1 << 0,
};

enum omap_dss_output_id {
	OMAP_DSS_OUTPUT_DPI	= 1 << 0,
	OMAP_DSS_OUTPUT_DBI	= 1 << 1,
	OMAP_DSS_OUTPUT_SDI	= 1 << 2,
	OMAP_DSS_OUTPUT_DSI1	= 1 << 3,
	OMAP_DSS_OUTPUT_DSI2	= 1 << 4,
	OMAP_DSS_OUTPUT_VENC	= 1 << 5,
	OMAP_DSS_OUTPUT_HDMI	= 1 << 6,
};

/* RFBI */

struct rfbi_timings {
	int cs_on_time;
	int cs_off_time;
	int we_on_time;
	int we_off_time;
	int re_on_time;
	int re_off_time;
	int we_cycle_time;
	int re_cycle_time;
	int cs_pulse_width;
	int access_time;

	int clk_div;

	u32 tim[5];             /* set by rfbi_convert_timings() */

	int converted;
};

/* DSI */

enum omap_dss_dsi_trans_mode {
	/* Sync Pulses: both sync start and end packets sent */
	OMAP_DSS_DSI_PULSE_MODE,
	/* Sync Events: only sync start packets sent */
	OMAP_DSS_DSI_EVENT_MODE,
	/* Burst: only sync start packets sent, pixels are time compressed */
	OMAP_DSS_DSI_BURST_MODE,
};

struct omap_dss_dsi_videomode_timings {
	unsigned long hsclk;

	unsigned ndl;
	unsigned bitspp;

	/* pixels */
	u16 hact;
	/* lines */
	u16 vact;

	/* DSI video mode blanking data */
	/* Unit: byte clock cycles */
	u16 hss;
	u16 hsa;
	u16 hse;
	u16 hfp;
	u16 hbp;
	/* Unit: line clocks */
	u16 vsa;
	u16 vfp;
	u16 vbp;

	/* DSI blanking modes */
	int blanking_mode;
	int hsa_blanking_mode;
	int hbp_blanking_mode;
	int hfp_blanking_mode;

	enum omap_dss_dsi_trans_mode trans_mode;

	bool ddr_clk_always_on;
	int window_sync;
};

struct omap_dss_dsi_config {
	enum omap_dss_dsi_mode mode;
	enum omap_dss_dsi_pixel_format pixel_format;
	const struct videomode *vm;

	unsigned long hs_clk_min, hs_clk_max;
	unsigned long lp_clk_min, lp_clk_max;

	bool ddr_clk_always_on;
	enum omap_dss_dsi_trans_mode trans_mode;
};

/* Hardcoded videomodes for tv. Venc only uses these to
 * identify the mode, and does not actually use the configs
 * itself. However, the configs should be something that
 * a normal monitor can also show */
extern const struct videomode omap_dss_pal_vm;
extern const struct videomode omap_dss_ntsc_vm;

struct omap_dss_cpr_coefs {
	s16 rr, rg, rb;
	s16 gr, gg, gb;
	s16 br, bg, bb;
};

struct omap_overlay_info {
	dma_addr_t paddr;
	dma_addr_t p_uv_addr;  /* for NV12 format */
	u16 screen_width;
	u16 width;
	u16 height;
	enum omap_color_mode color_mode;
	u8 rotation;
	enum omap_dss_rotation_type rotation_type;
	bool mirror;

	u16 pos_x;
	u16 pos_y;
	u16 out_width;	/* if 0, out_width == width */
	u16 out_height;	/* if 0, out_height == height */
	u8 global_alpha;
	u8 pre_mult_alpha;
	u8 zorder;
};

struct omap_overlay {
	struct kobject kobj;
	struct list_head list;

	/* static fields */
	const char *name;
	enum omap_plane_id id;
	enum omap_color_mode supported_modes;
	enum omap_overlay_caps caps;

	/* dynamic fields */
	struct omap_overlay_manager *manager;

	/*
	 * The following functions do not block:
	 *
	 * is_enabled
	 * set_overlay_info
	 * get_overlay_info
	 *
	 * The rest of the functions may block and cannot be called from
	 * interrupt context
	 */

	int (*enable)(struct omap_overlay *ovl);
	int (*disable)(struct omap_overlay *ovl);
	bool (*is_enabled)(struct omap_overlay *ovl);

	int (*set_manager)(struct omap_overlay *ovl,
		struct omap_overlay_manager *mgr);
	int (*unset_manager)(struct omap_overlay *ovl);

	int (*set_overlay_info)(struct omap_overlay *ovl,
			struct omap_overlay_info *info);
	void (*get_overlay_info)(struct omap_overlay *ovl,
			struct omap_overlay_info *info);

	int (*wait_for_go)(struct omap_overlay *ovl);

	struct omap_dss_device *(*get_device)(struct omap_overlay *ovl);
};

struct omap_overlay_manager_info {
	u32 default_color;

	enum omap_dss_trans_key_type trans_key_type;
	u32 trans_key;
	bool trans_enabled;

	bool partial_alpha_enabled;

	bool cpr_enable;
	struct omap_dss_cpr_coefs cpr_coefs;
};

struct omap_overlay_manager {
	struct kobject kobj;

	/* static fields */
	const char *name;
	enum omap_channel id;
	enum omap_overlay_manager_caps caps;
	struct list_head overlays;
	enum omap_display_type supported_displays;
	enum omap_dss_output_id supported_outputs;

	/* dynamic fields */
	struct omap_dss_device *output;

	/*
	 * The following functions do not block:
	 *
	 * set_manager_info
	 * get_manager_info
	 * apply
	 *
	 * The rest of the functions may block and cannot be called from
	 * interrupt context
	 */

	int (*set_output)(struct omap_overlay_manager *mgr,
		struct omap_dss_device *output);
	int (*unset_output)(struct omap_overlay_manager *mgr);

	int (*set_manager_info)(struct omap_overlay_manager *mgr,
			struct omap_overlay_manager_info *info);
	void (*get_manager_info)(struct omap_overlay_manager *mgr,
			struct omap_overlay_manager_info *info);

	int (*apply)(struct omap_overlay_manager *mgr);
	int (*wait_for_go)(struct omap_overlay_manager *mgr);
	int (*wait_for_vsync)(struct omap_overlay_manager *mgr);

	struct omap_dss_device *(*get_device)(struct omap_overlay_manager *mgr);
};

/* 22 pins means 1 clk lane and 10 data lanes */
#define OMAP_DSS_MAX_DSI_PINS 22

struct omap_dsi_pin_config {
	int num_pins;
	/*
	 * pin numbers in the following order:
	 * clk+, clk-
	 * data1+, data1-
	 * data2+, data2-
	 * ...
	 */
	int pins[OMAP_DSS_MAX_DSI_PINS];
};

struct omap_dss_writeback_info {
	u32 paddr;
	u32 p_uv_addr;
	u16 buf_width;
	u16 width;
	u16 height;
	enum omap_color_mode color_mode;
	u8 rotation;
	enum omap_dss_rotation_type rotation_type;
	bool mirror;
	u8 pre_mult_alpha;
};

struct omapdss_dpi_ops {
	int (*connect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);
	void (*disconnect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);

	int (*enable)(struct omap_dss_device *dssdev);
	void (*disable)(struct omap_dss_device *dssdev);

	int (*check_timings)(struct omap_dss_device *dssdev,
			     struct videomode *vm);
	void (*set_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
	void (*get_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
};

struct omapdss_sdi_ops {
	int (*connect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);
	void (*disconnect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);

	int (*enable)(struct omap_dss_device *dssdev);
	void (*disable)(struct omap_dss_device *dssdev);

	int (*check_timings)(struct omap_dss_device *dssdev,
			     struct videomode *vm);
	void (*set_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
	void (*get_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
};

struct omapdss_dvi_ops {
	int (*connect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);
	void (*disconnect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);

	int (*enable)(struct omap_dss_device *dssdev);
	void (*disable)(struct omap_dss_device *dssdev);

	int (*check_timings)(struct omap_dss_device *dssdev,
			     struct videomode *vm);
	void (*set_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
	void (*get_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
};

struct omapdss_atv_ops {
	int (*connect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);
	void (*disconnect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);

	int (*enable)(struct omap_dss_device *dssdev);
	void (*disable)(struct omap_dss_device *dssdev);

	int (*check_timings)(struct omap_dss_device *dssdev,
			     struct videomode *vm);
	void (*set_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
	void (*get_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);

	int (*set_wss)(struct omap_dss_device *dssdev, u32 wss);
	u32 (*get_wss)(struct omap_dss_device *dssdev);
};

struct omapdss_hdmi_ops {
	int (*connect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);
	void (*disconnect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);

	int (*enable)(struct omap_dss_device *dssdev);
	void (*disable)(struct omap_dss_device *dssdev);

	int (*check_timings)(struct omap_dss_device *dssdev,
			     struct videomode *vm);
	void (*set_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
	void (*get_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);

	int (*read_edid)(struct omap_dss_device *dssdev, u8 *buf, int len);
	bool (*detect)(struct omap_dss_device *dssdev);

	int (*set_hdmi_mode)(struct omap_dss_device *dssdev, bool hdmi_mode);
	int (*set_infoframe)(struct omap_dss_device *dssdev,
		const struct hdmi_avi_infoframe *avi);
};

struct omapdss_dsi_ops {
	int (*connect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);
	void (*disconnect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);

	int (*enable)(struct omap_dss_device *dssdev);
	void (*disable)(struct omap_dss_device *dssdev, bool disconnect_lanes,
			bool enter_ulps);

	/* bus configuration */
	int (*set_config)(struct omap_dss_device *dssdev,
			const struct omap_dss_dsi_config *cfg);
	int (*configure_pins)(struct omap_dss_device *dssdev,
			const struct omap_dsi_pin_config *pin_cfg);

	void (*enable_hs)(struct omap_dss_device *dssdev, int channel,
			bool enable);
	int (*enable_te)(struct omap_dss_device *dssdev, bool enable);

	int (*update)(struct omap_dss_device *dssdev, int channel,
			void (*callback)(int, void *), void *data);

	void (*bus_lock)(struct omap_dss_device *dssdev);
	void (*bus_unlock)(struct omap_dss_device *dssdev);

	int (*enable_video_output)(struct omap_dss_device *dssdev, int channel);
	void (*disable_video_output)(struct omap_dss_device *dssdev,
			int channel);

	int (*request_vc)(struct omap_dss_device *dssdev, int *channel);
	int (*set_vc_id)(struct omap_dss_device *dssdev, int channel,
			int vc_id);
	void (*release_vc)(struct omap_dss_device *dssdev, int channel);

	/* data transfer */
	int (*dcs_write)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*dcs_write_nosync)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*dcs_read)(struct omap_dss_device *dssdev, int channel, u8 dcs_cmd,
			u8 *data, int len);

	int (*gen_write)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*gen_write_nosync)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*gen_read)(struct omap_dss_device *dssdev, int channel,
			u8 *reqdata, int reqlen,
			u8 *data, int len);

	int (*bta_sync)(struct omap_dss_device *dssdev, int channel);

	int (*set_max_rx_packet_size)(struct omap_dss_device *dssdev,
			int channel, u16 plen);
};

struct omap_dss_device {
	struct kobject kobj;
	struct device *dev;

	struct module *owner;

	struct list_head panel_list;

	/* alias in the form of "display%d" */
	char alias[16];

	enum omap_display_type type;
	enum omap_display_type output_type;

	union {
		struct {
			u8 channel;
			u8 data_lines;
		} rfbi;

		struct {
			int module;
		} dsi;
	} phy;

	struct {
		struct videomode vm;

		enum omap_dss_dsi_pixel_format dsi_pix_fmt;
		enum omap_dss_dsi_mode dsi_mode;
	} panel;

	struct {
		u8 pixel_size;
		struct rfbi_timings rfbi_timings;
	} ctrl;

	const char *name;

	/* used to match device to driver */
	const char *driver_name;

	void *data;

	struct omap_dss_driver *driver;

	union {
		const struct omapdss_dpi_ops *dpi;
		const struct omapdss_sdi_ops *sdi;
		const struct omapdss_dvi_ops *dvi;
		const struct omapdss_hdmi_ops *hdmi;
		const struct omapdss_atv_ops *atv;
		const struct omapdss_dsi_ops *dsi;
	} ops;

	/* helper variable for driver suspend/resume */
	bool activate_after_resume;

	enum omap_display_caps caps;

	struct omap_dss_device *src;

	enum omap_dss_display_state state;

	/* OMAP DSS output specific fields */

	struct list_head list;

	/* DISPC channel for this output */
	enum omap_channel dispc_channel;
	bool dispc_channel_connected;

	/* output instance */
	enum omap_dss_output_id id;

	/* the port number in the DT node */
	int port_num;

	/* dynamic fields */
	struct omap_overlay_manager *manager;

	struct omap_dss_device *dst;
};

struct omap_dss_driver {
	int (*probe)(struct omap_dss_device *);
	void (*remove)(struct omap_dss_device *);

	int (*connect)(struct omap_dss_device *dssdev);
	void (*disconnect)(struct omap_dss_device *dssdev);

	int (*enable)(struct omap_dss_device *display);
	void (*disable)(struct omap_dss_device *display);
	int (*run_test)(struct omap_dss_device *display, int test);

	int (*update)(struct omap_dss_device *dssdev,
			       u16 x, u16 y, u16 w, u16 h);
	int (*sync)(struct omap_dss_device *dssdev);

	int (*enable_te)(struct omap_dss_device *dssdev, bool enable);
	int (*get_te)(struct omap_dss_device *dssdev);

	u8 (*get_rotate)(struct omap_dss_device *dssdev);
	int (*set_rotate)(struct omap_dss_device *dssdev, u8 rotate);

	bool (*get_mirror)(struct omap_dss_device *dssdev);
	int (*set_mirror)(struct omap_dss_device *dssdev, bool enable);

	int (*memory_read)(struct omap_dss_device *dssdev,
			void *buf, size_t size,
			u16 x, u16 y, u16 w, u16 h);

	void (*get_resolution)(struct omap_dss_device *dssdev,
			u16 *xres, u16 *yres);
	void (*get_dimensions)(struct omap_dss_device *dssdev,
			u32 *width, u32 *height);

	int (*check_timings)(struct omap_dss_device *dssdev,
			     struct videomode *vm);
	void (*set_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);
	void (*get_timings)(struct omap_dss_device *dssdev,
			    struct videomode *vm);

	int (*set_wss)(struct omap_dss_device *dssdev, u32 wss);
	u32 (*get_wss)(struct omap_dss_device *dssdev);

	int (*read_edid)(struct omap_dss_device *dssdev, u8 *buf, int len);
	bool (*detect)(struct omap_dss_device *dssdev);

	int (*set_hdmi_mode)(struct omap_dss_device *dssdev, bool hdmi_mode);
	int (*set_hdmi_infoframe)(struct omap_dss_device *dssdev,
		const struct hdmi_avi_infoframe *avi);
};

enum omapdss_version omapdss_get_version(void);
bool omapdss_is_initialized(void);

int omap_dss_register_driver(struct omap_dss_driver *);
void omap_dss_unregister_driver(struct omap_dss_driver *);

int omapdss_register_display(struct omap_dss_device *dssdev);
void omapdss_unregister_display(struct omap_dss_device *dssdev);

struct omap_dss_device *omap_dss_get_device(struct omap_dss_device *dssdev);
void omap_dss_put_device(struct omap_dss_device *dssdev);
#define for_each_dss_dev(d) while ((d = omap_dss_get_next_device(d)) != NULL)
struct omap_dss_device *omap_dss_get_next_device(struct omap_dss_device *from);
struct omap_dss_device *omap_dss_find_device(void *data,
		int (*match)(struct omap_dss_device *dssdev, void *data));

int dss_feat_get_num_mgrs(void);
int dss_feat_get_num_ovls(void);
enum omap_color_mode dss_feat_get_supported_color_modes(enum omap_plane_id plane);



int omap_dss_get_num_overlay_managers(void);
struct omap_overlay_manager *omap_dss_get_overlay_manager(int num);

int omap_dss_get_num_overlays(void);
struct omap_overlay *omap_dss_get_overlay(int num);

int omapdss_register_output(struct omap_dss_device *output);
void omapdss_unregister_output(struct omap_dss_device *output);
struct omap_dss_device *omap_dss_get_output(enum omap_dss_output_id id);
struct omap_dss_device *omap_dss_find_output(const char *name);
struct omap_dss_device *omap_dss_find_output_by_port_node(struct device_node *port);
int omapdss_output_set_device(struct omap_dss_device *out,
		struct omap_dss_device *dssdev);
int omapdss_output_unset_device(struct omap_dss_device *out);

struct omap_dss_device *omapdss_find_output_from_display(struct omap_dss_device *dssdev);
struct omap_overlay_manager *omapdss_find_mgr_from_display(struct omap_dss_device *dssdev);

void omapdss_default_get_resolution(struct omap_dss_device *dssdev,
		u16 *xres, u16 *yres);
void omapdss_default_get_timings(struct omap_dss_device *dssdev,
				 struct videomode *vm);

typedef void (*omap_dispc_isr_t) (void *arg, u32 mask);
int omap_dispc_register_isr(omap_dispc_isr_t isr, void *arg, u32 mask);
int omap_dispc_unregister_isr(omap_dispc_isr_t isr, void *arg, u32 mask);

int omapdss_compat_init(void);
void omapdss_compat_uninit(void);

static inline bool omapdss_device_is_connected(struct omap_dss_device *dssdev)
{
	return dssdev->src;
}

static inline bool omapdss_device_is_enabled(struct omap_dss_device *dssdev)
{
	return dssdev->state == OMAP_DSS_DISPLAY_ACTIVE;
}

struct omap_dss_device *
omapdss_of_find_source_for_first_ep(struct device_node *node);

void omapdss_set_is_initialized(bool set);

struct device_node *dss_of_port_get_parent_device(struct device_node *port);
u32 dss_of_port_get_port_number(struct device_node *port);

struct dss_mgr_ops {
	int (*connect)(enum omap_channel channel,
		struct omap_dss_device *dst);
	void (*disconnect)(enum omap_channel channel,
		struct omap_dss_device *dst);

	void (*start_update)(enum omap_channel channel);
	int (*enable)(enum omap_channel channel);
	void (*disable)(enum omap_channel channel);
	void (*set_timings)(enum omap_channel channel,
			const struct videomode *vm);
	void (*set_lcd_config)(enum omap_channel channel,
			const struct dss_lcd_mgr_config *config);
	int (*register_framedone_handler)(enum omap_channel channel,
			void (*handler)(void *), void *data);
	void (*unregister_framedone_handler)(enum omap_channel channel,
			void (*handler)(void *), void *data);
};

int dss_install_mgr_ops(const struct dss_mgr_ops *mgr_ops);
void dss_uninstall_mgr_ops(void);

int dss_mgr_connect(enum omap_channel channel,
		struct omap_dss_device *dst);
void dss_mgr_disconnect(enum omap_channel channel,
		struct omap_dss_device *dst);
void dss_mgr_set_timings(enum omap_channel channel,
		const struct videomode *vm);
void dss_mgr_set_lcd_config(enum omap_channel channel,
		const struct dss_lcd_mgr_config *config);
int dss_mgr_enable(enum omap_channel channel);
void dss_mgr_disable(enum omap_channel channel);
void dss_mgr_start_update(enum omap_channel channel);
int dss_mgr_register_framedone_handler(enum omap_channel channel,
		void (*handler)(void *), void *data);
void dss_mgr_unregister_framedone_handler(enum omap_channel channel,
		void (*handler)(void *), void *data);

/* dispc ops */

struct dispc_ops {
	u32 (*read_irqstatus)(void);
	void (*clear_irqstatus)(u32 mask);
	void (*write_irqenable)(u32 mask);

	int (*request_irq)(irq_handler_t handler, void *dev_id);
	void (*free_irq)(void *dev_id);

	int (*runtime_get)(void);
	void (*runtime_put)(void);

	int (*get_num_ovls)(void);
	int (*get_num_mgrs)(void);

	void (*mgr_enable)(enum omap_channel channel, bool enable);
	bool (*mgr_is_enabled)(enum omap_channel channel);
	u32 (*mgr_get_vsync_irq)(enum omap_channel channel);
	u32 (*mgr_get_framedone_irq)(enum omap_channel channel);
	u32 (*mgr_get_sync_lost_irq)(enum omap_channel channel);
	bool (*mgr_go_busy)(enum omap_channel channel);
	void (*mgr_go)(enum omap_channel channel);
	void (*mgr_set_lcd_config)(enum omap_channel channel,
			const struct dss_lcd_mgr_config *config);
	void (*mgr_set_timings)(enum omap_channel channel,
			const struct videomode *vm);
	void (*mgr_setup)(enum omap_channel channel,
			const struct omap_overlay_manager_info *info);
	enum omap_dss_output_id (*mgr_get_supported_outputs)(enum omap_channel channel);
	u32 (*mgr_gamma_size)(enum omap_channel channel);
	void (*mgr_set_gamma)(enum omap_channel channel,
		const struct drm_color_lut *lut,
		unsigned int length);

	int (*ovl_enable)(enum omap_plane_id plane, bool enable);
	int (*ovl_setup)(enum omap_plane_id plane,
			 const struct omap_overlay_info *oi,
			const struct videomode *vm, bool mem_to_mem,
			enum omap_channel channel);

	enum omap_color_mode (*ovl_get_color_modes)(enum omap_plane_id plane);
};

void dispc_set_ops(const struct dispc_ops *o);
const struct dispc_ops *dispc_get_ops(void);

bool omapdss_component_is_display(struct device_node *node);
bool omapdss_component_is_output(struct device_node *node);

bool omapdss_stack_is_ready(void);
void omapdss_gather_components(struct device *dev);

#endif /* __OMAP_DRM_DSS_H */
