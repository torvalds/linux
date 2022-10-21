/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#ifndef _MTK_DISP_DRV_H_
#define _MTK_DISP_DRV_H_

#include <linux/soc/mediatek/mtk-cmdq.h>
#include "mtk_drm_plane.h"
#include "mtk_mdp_rdma.h"

int mtk_aal_clk_enable(struct device *dev);
void mtk_aal_clk_disable(struct device *dev);
void mtk_aal_config(struct device *dev, unsigned int w,
		    unsigned int h, unsigned int vrefresh,
		    unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
void mtk_aal_gamma_set(struct device *dev, struct drm_crtc_state *state);
void mtk_aal_start(struct device *dev);
void mtk_aal_stop(struct device *dev);

void mtk_ccorr_ctm_set(struct device *dev, struct drm_crtc_state *state);
int mtk_ccorr_clk_enable(struct device *dev);
void mtk_ccorr_clk_disable(struct device *dev);
void mtk_ccorr_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
void mtk_ccorr_start(struct device *dev);
void mtk_ccorr_stop(struct device *dev);

void mtk_color_bypass_shadow(struct device *dev);
int mtk_color_clk_enable(struct device *dev);
void mtk_color_clk_disable(struct device *dev);
void mtk_color_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
void mtk_color_start(struct device *dev);

void mtk_dither_set_common(void __iomem *regs, struct cmdq_client_reg *cmdq_reg,
			   unsigned int bpc, unsigned int cfg,
			   unsigned int dither_en, struct cmdq_pkt *cmdq_pkt);

void mtk_dpi_start(struct device *dev);
void mtk_dpi_stop(struct device *dev);

void mtk_dsi_ddp_start(struct device *dev);
void mtk_dsi_ddp_stop(struct device *dev);

int mtk_gamma_clk_enable(struct device *dev);
void mtk_gamma_clk_disable(struct device *dev);
void mtk_gamma_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
void mtk_gamma_set(struct device *dev, struct drm_crtc_state *state);
void mtk_gamma_set_common(void __iomem *regs, struct drm_crtc_state *state, bool lut_diff);
void mtk_gamma_start(struct device *dev);
void mtk_gamma_stop(struct device *dev);

int mtk_merge_clk_enable(struct device *dev);
void mtk_merge_clk_disable(struct device *dev);
void mtk_merge_config(struct device *dev, unsigned int width,
		      unsigned int height, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
void mtk_merge_start(struct device *dev);
void mtk_merge_stop(struct device *dev);
void mtk_merge_advance_config(struct device *dev, unsigned int l_w, unsigned int r_w,
			      unsigned int h, unsigned int vrefresh, unsigned int bpc,
			      struct cmdq_pkt *cmdq_pkt);
void mtk_merge_start_cmdq(struct device *dev, struct cmdq_pkt *cmdq_pkt);
void mtk_merge_stop_cmdq(struct device *dev, struct cmdq_pkt *cmdq_pkt);

void mtk_ovl_bgclr_in_on(struct device *dev);
void mtk_ovl_bgclr_in_off(struct device *dev);
void mtk_ovl_bypass_shadow(struct device *dev);
int mtk_ovl_clk_enable(struct device *dev);
void mtk_ovl_clk_disable(struct device *dev);
void mtk_ovl_config(struct device *dev, unsigned int w,
		    unsigned int h, unsigned int vrefresh,
		    unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
int mtk_ovl_layer_check(struct device *dev, unsigned int idx,
			struct mtk_plane_state *mtk_state);
void mtk_ovl_layer_config(struct device *dev, unsigned int idx,
			  struct mtk_plane_state *state,
			  struct cmdq_pkt *cmdq_pkt);
unsigned int mtk_ovl_layer_nr(struct device *dev);
void mtk_ovl_layer_on(struct device *dev, unsigned int idx,
		      struct cmdq_pkt *cmdq_pkt);
void mtk_ovl_layer_off(struct device *dev, unsigned int idx,
		       struct cmdq_pkt *cmdq_pkt);
void mtk_ovl_start(struct device *dev);
void mtk_ovl_stop(struct device *dev);
unsigned int mtk_ovl_supported_rotations(struct device *dev);
void mtk_ovl_register_vblank_cb(struct device *dev,
				void (*vblank_cb)(void *),
				void *vblank_cb_data);
void mtk_ovl_unregister_vblank_cb(struct device *dev);
void mtk_ovl_enable_vblank(struct device *dev);
void mtk_ovl_disable_vblank(struct device *dev);

void mtk_rdma_bypass_shadow(struct device *dev);
int mtk_rdma_clk_enable(struct device *dev);
void mtk_rdma_clk_disable(struct device *dev);
void mtk_rdma_config(struct device *dev, unsigned int width,
		     unsigned int height, unsigned int vrefresh,
		     unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
unsigned int mtk_rdma_layer_nr(struct device *dev);
void mtk_rdma_layer_config(struct device *dev, unsigned int idx,
			   struct mtk_plane_state *state,
			   struct cmdq_pkt *cmdq_pkt);
void mtk_rdma_start(struct device *dev);
void mtk_rdma_stop(struct device *dev);
void mtk_rdma_register_vblank_cb(struct device *dev,
				 void (*vblank_cb)(void *),
				 void *vblank_cb_data);
void mtk_rdma_unregister_vblank_cb(struct device *dev);
void mtk_rdma_enable_vblank(struct device *dev);
void mtk_rdma_disable_vblank(struct device *dev);

int mtk_mdp_rdma_clk_enable(struct device *dev);
void mtk_mdp_rdma_clk_disable(struct device *dev);
void mtk_mdp_rdma_start(struct device *dev, struct cmdq_pkt *cmdq_pkt);
void mtk_mdp_rdma_stop(struct device *dev, struct cmdq_pkt *cmdq_pkt);
void mtk_mdp_rdma_config(struct device *dev, struct mtk_mdp_rdma_cfg *cfg,
			 struct cmdq_pkt *cmdq_pkt);
#endif
