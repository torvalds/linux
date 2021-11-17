/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __OMAP_DRM_DSS_H
#define __OMAP_DRM_DSS_H

#include <drm/drm_color_mgmt.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mode.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/platform_data/omapdss.h>
#include <video/videomode.h>

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

struct dispc_device;
struct drm_connector;
struct dss_device;
struct dss_lcd_mgr_config;
struct hdmi_avi_infoframe;
struct omap_drm_private;
struct omap_dss_device;
struct snd_aes_iec958;
struct snd_cea_861_aud_if;

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
	_UNUSED_,
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

enum omap_dss_rotation_type {
	OMAP_DSS_ROT_NONE	= 0,
	OMAP_DSS_ROT_TILER	= 1 << 0,
};

enum omap_overlay_caps {
	OMAP_DSS_OVL_CAP_SCALE = 1 << 0,
	OMAP_DSS_OVL_CAP_GLOBAL_ALPHA = 1 << 1,
	OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA = 1 << 2,
	OMAP_DSS_OVL_CAP_ZORDER = 1 << 3,
	OMAP_DSS_OVL_CAP_POS = 1 << 4,
	OMAP_DSS_OVL_CAP_REPLICATION = 1 << 5,
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
	u32 fourcc;
	u8 rotation;
	enum omap_dss_rotation_type rotation_type;

	u16 pos_x;
	u16 pos_y;
	u16 out_width;	/* if 0, out_width == width */
	u16 out_height;	/* if 0, out_height == height */
	u8 global_alpha;
	u8 pre_mult_alpha;
	u8 zorder;

	enum drm_color_encoding color_encoding;
	enum drm_color_range color_range;
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

struct omap_dss_writeback_info {
	u32 paddr;
	u32 p_uv_addr;
	u16 buf_width;
	u16 width;
	u16 height;
	u32 fourcc;
	u8 rotation;
	enum omap_dss_rotation_type rotation_type;
	u8 pre_mult_alpha;
};

struct omapdss_dsi_ops {
	int (*update)(struct omap_dss_device *dssdev);
	bool (*is_video_mode)(struct omap_dss_device *dssdev);
};

struct omap_dss_device {
	struct device *dev;

	struct dss_device *dss;
	struct drm_bridge *bridge;
	struct drm_bridge *next_bridge;
	struct drm_panel *panel;

	struct list_head list;

	/*
	 * DSS type that this device generates (for DSS internal devices) or
	 * requires (for external encoders, connectors and panels). Must be a
	 * non-zero (different than OMAP_DISPLAY_TYPE_NONE) value.
	 */
	enum omap_display_type type;

	const char *name;

	const struct omapdss_dsi_ops *dsi_ops;
	u32 bus_flags;

	/* OMAP DSS output specific fields */

	/* DISPC channel for this output */
	enum omap_channel dispc_channel;

	/* output instance */
	enum omap_dss_output_id id;

	/* port number in DT */
	unsigned int of_port;
};

struct dss_pdata {
	struct dss_device *dss;
};

void omapdss_device_register(struct omap_dss_device *dssdev);
void omapdss_device_unregister(struct omap_dss_device *dssdev);
struct omap_dss_device *omapdss_device_get(struct omap_dss_device *dssdev);
void omapdss_device_put(struct omap_dss_device *dssdev);
struct omap_dss_device *omapdss_find_device_by_node(struct device_node *node);
int omapdss_device_connect(struct dss_device *dss,
			   struct omap_dss_device *src,
			   struct omap_dss_device *dst);
void omapdss_device_disconnect(struct omap_dss_device *src,
			       struct omap_dss_device *dst);

int omap_dss_get_num_overlay_managers(void);

int omap_dss_get_num_overlays(void);

#define for_each_dss_output(d) \
	while ((d = omapdss_device_next_output(d)) != NULL)
struct omap_dss_device *omapdss_device_next_output(struct omap_dss_device *from);
int omapdss_device_init_output(struct omap_dss_device *out,
			       struct drm_bridge *local_bridge);
void omapdss_device_cleanup_output(struct omap_dss_device *out);

typedef void (*omap_dispc_isr_t) (void *arg, u32 mask);
int omap_dispc_register_isr(omap_dispc_isr_t isr, void *arg, u32 mask);
int omap_dispc_unregister_isr(omap_dispc_isr_t isr, void *arg, u32 mask);

int omapdss_compat_init(void);
void omapdss_compat_uninit(void);

enum dss_writeback_channel {
	DSS_WB_LCD1_MGR =	0,
	DSS_WB_LCD2_MGR =	1,
	DSS_WB_TV_MGR =		2,
	DSS_WB_OVL0 =		3,
	DSS_WB_OVL1 =		4,
	DSS_WB_OVL2 =		5,
	DSS_WB_OVL3 =		6,
	DSS_WB_LCD3_MGR =	7,
};

void omap_crtc_dss_start_update(struct omap_drm_private *priv,
				       enum omap_channel channel);
void omap_crtc_set_enabled(struct drm_crtc *crtc, bool enable);
int omap_crtc_dss_enable(struct omap_drm_private *priv, enum omap_channel channel);
void omap_crtc_dss_disable(struct omap_drm_private *priv, enum omap_channel channel);
void omap_crtc_dss_set_timings(struct omap_drm_private *priv,
		enum omap_channel channel,
		const struct videomode *vm);
void omap_crtc_dss_set_lcd_config(struct omap_drm_private *priv,
		enum omap_channel channel,
		const struct dss_lcd_mgr_config *config);
int omap_crtc_dss_register_framedone(
		struct omap_drm_private *priv, enum omap_channel channel,
		void (*handler)(void *), void *data);
void omap_crtc_dss_unregister_framedone(
		struct omap_drm_private *priv, enum omap_channel channel,
		void (*handler)(void *), void *data);

void dss_mgr_set_timings(struct omap_dss_device *dssdev,
		const struct videomode *vm);
void dss_mgr_set_lcd_config(struct omap_dss_device *dssdev,
		const struct dss_lcd_mgr_config *config);
int dss_mgr_enable(struct omap_dss_device *dssdev);
void dss_mgr_disable(struct omap_dss_device *dssdev);
void dss_mgr_start_update(struct omap_dss_device *dssdev);
int dss_mgr_register_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data);
void dss_mgr_unregister_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data);

struct dispc_device *dispc_get_dispc(struct dss_device *dss);

bool omapdss_stack_is_ready(void);
void omapdss_gather_components(struct device *dev);

int omap_dss_init(void);
void omap_dss_exit(void);

#endif /* __OMAP_DRM_DSS_H */
