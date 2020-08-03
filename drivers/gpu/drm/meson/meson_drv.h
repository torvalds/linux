/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __MESON_DRV_H
#define __MESON_DRV_H

#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

struct drm_crtc;
struct drm_device;
struct drm_plane;
struct meson_drm;
struct meson_afbcd_ops;

enum vpu_compatible {
	VPU_COMPATIBLE_GXBB = 0,
	VPU_COMPATIBLE_GXL  = 1,
	VPU_COMPATIBLE_GXM  = 2,
	VPU_COMPATIBLE_G12A = 3,
};

struct meson_drm_match_data {
	enum vpu_compatible compat;
	struct meson_afbcd_ops *afbcd_ops;
};

struct meson_drm_soc_limits {
	unsigned int max_hdmi_phy_freq;
};

struct meson_drm {
	struct device *dev;
	enum vpu_compatible compat;
	void __iomem *io_base;
	struct regmap *hhi;
	int vsync_irq;

	struct meson_canvas *canvas;
	u8 canvas_id_osd1;
	u8 canvas_id_vd1_0;
	u8 canvas_id_vd1_1;
	u8 canvas_id_vd1_2;

	struct drm_device *drm;
	struct drm_crtc *crtc;
	struct drm_plane *primary_plane;
	struct drm_plane *overlay_plane;

	const struct meson_drm_soc_limits *limits;

	/* Components Data */
	struct {
		bool osd1_enabled;
		bool osd1_interlace;
		bool osd1_commit;
		bool osd1_afbcd;
		uint32_t osd1_ctrl_stat;
		uint32_t osd1_ctrl_stat2;
		uint32_t osd1_blk0_cfg[5];
		uint32_t osd1_blk1_cfg4;
		uint32_t osd1_blk2_cfg4;
		uint32_t osd1_addr;
		uint32_t osd1_stride;
		uint32_t osd1_height;
		uint32_t osd1_width;
		uint32_t osd_sc_ctrl0;
		uint32_t osd_sc_i_wh_m1;
		uint32_t osd_sc_o_h_start_end;
		uint32_t osd_sc_o_v_start_end;
		uint32_t osd_sc_v_ini_phase;
		uint32_t osd_sc_v_phase_step;
		uint32_t osd_sc_h_ini_phase;
		uint32_t osd_sc_h_phase_step;
		uint32_t osd_sc_h_ctrl0;
		uint32_t osd_sc_v_ctrl0;
		uint32_t osd_blend_din0_scope_h;
		uint32_t osd_blend_din0_scope_v;
		uint32_t osb_blend0_size;
		uint32_t osb_blend1_size;

		bool vd1_enabled;
		bool vd1_commit;
		unsigned int vd1_planes;
		uint32_t vd1_if0_gen_reg;
		uint32_t vd1_if0_luma_x0;
		uint32_t vd1_if0_luma_y0;
		uint32_t vd1_if0_chroma_x0;
		uint32_t vd1_if0_chroma_y0;
		uint32_t vd1_if0_repeat_loop;
		uint32_t vd1_if0_luma0_rpt_pat;
		uint32_t vd1_if0_chroma0_rpt_pat;
		uint32_t vd1_range_map_y;
		uint32_t vd1_range_map_cb;
		uint32_t vd1_range_map_cr;
		uint32_t viu_vd1_fmt_w;
		uint32_t vd1_if0_canvas0;
		uint32_t vd1_if0_gen_reg2;
		uint32_t viu_vd1_fmt_ctrl;
		uint32_t vd1_addr0;
		uint32_t vd1_addr1;
		uint32_t vd1_addr2;
		uint32_t vd1_stride0;
		uint32_t vd1_stride1;
		uint32_t vd1_stride2;
		uint32_t vd1_height0;
		uint32_t vd1_height1;
		uint32_t vd1_height2;
		uint32_t vpp_pic_in_height;
		uint32_t vpp_postblend_vd1_h_start_end;
		uint32_t vpp_postblend_vd1_v_start_end;
		uint32_t vpp_hsc_region12_startp;
		uint32_t vpp_hsc_region34_startp;
		uint32_t vpp_hsc_region4_endp;
		uint32_t vpp_hsc_start_phase_step;
		uint32_t vpp_hsc_region1_phase_slope;
		uint32_t vpp_hsc_region3_phase_slope;
		uint32_t vpp_line_in_length;
		uint32_t vpp_preblend_h_size;
		uint32_t vpp_vsc_region12_startp;
		uint32_t vpp_vsc_region34_startp;
		uint32_t vpp_vsc_region4_endp;
		uint32_t vpp_vsc_start_phase_step;
		uint32_t vpp_vsc_ini_phase;
		uint32_t vpp_vsc_phase_ctrl;
		uint32_t vpp_hsc_phase_ctrl;
		uint32_t vpp_blend_vd2_h_start_end;
		uint32_t vpp_blend_vd2_v_start_end;
	} viu;

	struct {
		unsigned int current_mode;
		bool hdmi_repeat;
		bool venc_repeat;
		bool hdmi_use_enci;
	} venc;

	struct {
		dma_addr_t addr_dma;
		uint32_t *addr;
		unsigned int offset;
	} rdma;

	struct {
		struct meson_afbcd_ops *ops;
		u64 modifier;
		u32 format;
	} afbcd;
};

static inline int meson_vpu_is_compatible(struct meson_drm *priv,
					  enum vpu_compatible family)
{
	return priv->compat == family;
}

#endif /* __MESON_DRV_H */
