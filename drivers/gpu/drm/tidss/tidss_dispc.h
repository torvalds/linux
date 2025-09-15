/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __TIDSS_DISPC_H__
#define __TIDSS_DISPC_H__

#include <drm/drm_color_mgmt.h>

#include "tidss_drv.h"

struct dispc_device;

struct drm_crtc_state;
struct drm_plane_state;

enum tidss_gamma_type { TIDSS_GAMMA_8BIT, TIDSS_GAMMA_10BIT };

struct tidss_vp_feat {
	struct tidss_vp_color_feat {
		u32 gamma_size;
		enum tidss_gamma_type gamma_type;
		bool has_ctm;
	} color;
};

struct tidss_plane_feat {
	struct tidss_plane_color_feat {
		u32 encodings;
		u32 ranges;
		enum drm_color_encoding default_encoding;
		enum drm_color_range default_range;
	} color;
	struct tidss_plane_blend_feat {
		bool global_alpha;
	} blend;
};

struct dispc_features_scaling {
	u32 in_width_max_5tap_rgb;
	u32 in_width_max_3tap_rgb;
	u32 in_width_max_5tap_yuv;
	u32 in_width_max_3tap_yuv;
	u32 upscale_limit;
	u32 downscale_limit_5tap;
	u32 downscale_limit_3tap;
	u32 xinc_max;
};

struct dispc_vid_info {
	const char *name; /* Should match dt reg names */
	u32 hw_id;
	bool is_lite;
};

struct dispc_errata {
	bool i2000; /* DSS Does Not Support YUV Pixel Data Formats */
};

enum dispc_vp_bus_type {
	DISPC_VP_DPI,		/* DPI output */
	DISPC_VP_OLDI_AM65X,	/* OLDI (LVDS) output for AM65x DSS */
	DISPC_VP_INTERNAL,	/* SoC internal routing */
	DISPC_VP_TIED_OFF,	/* Tied off / Unavailable */
	DISPC_VP_MAX_BUS_TYPE,
};

enum dispc_dss_subrevision {
	DISPC_K2G,
	DISPC_AM625,
	DISPC_AM62L,
	DISPC_AM62A7,
	DISPC_AM65X,
	DISPC_J721E,
};

struct dispc_features {
	int min_pclk_khz;
	int max_pclk_khz[DISPC_VP_MAX_BUS_TYPE];

	struct dispc_features_scaling scaling;

	enum dispc_dss_subrevision subrev;

	const char *common;
	const u16 *common_regs;
	u32 num_vps;
	const char *vp_name[TIDSS_MAX_PORTS]; /* Should match dt reg names */
	const char *ovr_name[TIDSS_MAX_PORTS]; /* Should match dt reg names */
	const char *vpclk_name[TIDSS_MAX_PORTS]; /* Should match dt clk names */
	const enum dispc_vp_bus_type vp_bus_type[TIDSS_MAX_PORTS];
	struct tidss_vp_feat vp_feat;
	u32 num_vids;
	struct dispc_vid_info vid_info[TIDSS_MAX_PLANES];
	u32 vid_order[TIDSS_MAX_PLANES];
};

extern const struct dispc_features dispc_k2g_feats;
extern const struct dispc_features dispc_am625_feats;
extern const struct dispc_features dispc_am62a7_feats;
extern const struct dispc_features dispc_am62l_feats;
extern const struct dispc_features dispc_am65x_feats;
extern const struct dispc_features dispc_j721e_feats;

int tidss_configure_oldi(struct tidss_device *tidss, u32 hw_videoport,
			 u32 oldi_cfg);
void tidss_disable_oldi(struct tidss_device *tidss, u32 hw_videoport);
unsigned int dispc_pclk_diff(unsigned long rate, unsigned long real_rate);

void dispc_set_irqenable(struct dispc_device *dispc, dispc_irq_t mask);
dispc_irq_t dispc_read_and_clear_irqstatus(struct dispc_device *dispc);

void dispc_ovr_set_plane(struct dispc_device *dispc, u32 hw_plane,
			 u32 hw_videoport, u32 x, u32 y, u32 layer);
void dispc_ovr_enable_layer(struct dispc_device *dispc,
			    u32 hw_videoport, u32 layer, bool enable);

void dispc_vp_prepare(struct dispc_device *dispc, u32 hw_videoport,
		      const struct drm_crtc_state *state);
void dispc_vp_enable(struct dispc_device *dispc, u32 hw_videoport,
		     const struct drm_crtc_state *state);
void dispc_vp_disable(struct dispc_device *dispc, u32 hw_videoport);
void dispc_vp_unprepare(struct dispc_device *dispc, u32 hw_videoport);
bool dispc_vp_go_busy(struct dispc_device *dispc, u32 hw_videoport);
void dispc_vp_go(struct dispc_device *dispc, u32 hw_videoport);
int dispc_vp_bus_check(struct dispc_device *dispc, u32 hw_videoport,
		       const struct drm_crtc_state *state);
enum drm_mode_status dispc_vp_mode_valid(struct dispc_device *dispc,
					 u32 hw_videoport,
					 const struct drm_display_mode *mode);
int dispc_vp_enable_clk(struct dispc_device *dispc, u32 hw_videoport);
void dispc_vp_disable_clk(struct dispc_device *dispc, u32 hw_videoport);
int dispc_vp_set_clk_rate(struct dispc_device *dispc, u32 hw_videoport,
			  unsigned long rate);
void dispc_vp_setup(struct dispc_device *dispc, u32 hw_videoport,
		    const struct drm_crtc_state *state, bool newmodeset);

int dispc_runtime_suspend(struct dispc_device *dispc);
int dispc_runtime_resume(struct dispc_device *dispc);

int dispc_plane_check(struct dispc_device *dispc, u32 hw_plane,
		      const struct drm_plane_state *state,
		      u32 hw_videoport);
void dispc_plane_setup(struct dispc_device *dispc, u32 hw_plane,
		       const struct drm_plane_state *state,
		       u32 hw_videoport);
void dispc_plane_enable(struct dispc_device *dispc, u32 hw_plane, bool enable);
const u32 *dispc_plane_formats(struct dispc_device *dispc, unsigned int *len);

int dispc_init(struct tidss_device *tidss);
void dispc_remove(struct tidss_device *tidss);

#endif
