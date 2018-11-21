/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#ifndef __MDP4_KMS_H__
#define __MDP4_KMS_H__

#include <drm/drm_panel.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "disp/mdp_kms.h"
#include "mdp4.xml.h"

struct device_node;

struct mdp4_kms {
	struct mdp_kms base;

	struct drm_device *dev;

	int rev;

	void __iomem *mmio;

	struct regulator *vdd;

	struct clk *clk;
	struct clk *pclk;
	struct clk *lut_clk;
	struct clk *axi_clk;

	struct mdp_irq error_handler;

	bool rpm_enabled;

	/* empty/blank cursor bo to use when cursor is "disabled" */
	struct drm_gem_object *blank_cursor_bo;
	uint64_t blank_cursor_iova;
};
#define to_mdp4_kms(x) container_of(x, struct mdp4_kms, base)

/* platform config data (ie. from DT, or pdata) */
struct mdp4_platform_config {
	struct iommu_domain *iommu;
	uint32_t max_clk;
};

static inline void mdp4_write(struct mdp4_kms *mdp4_kms, u32 reg, u32 data)
{
	msm_writel(data, mdp4_kms->mmio + reg);
}

static inline u32 mdp4_read(struct mdp4_kms *mdp4_kms, u32 reg)
{
	return msm_readl(mdp4_kms->mmio + reg);
}

static inline uint32_t pipe2flush(enum mdp4_pipe pipe)
{
	switch (pipe) {
	case VG1:      return MDP4_OVERLAY_FLUSH_VG1;
	case VG2:      return MDP4_OVERLAY_FLUSH_VG2;
	case RGB1:     return MDP4_OVERLAY_FLUSH_RGB1;
	case RGB2:     return MDP4_OVERLAY_FLUSH_RGB2;
	default:       return 0;
	}
}

static inline uint32_t ovlp2flush(int ovlp)
{
	switch (ovlp) {
	case 0:        return MDP4_OVERLAY_FLUSH_OVLP0;
	case 1:        return MDP4_OVERLAY_FLUSH_OVLP1;
	default:       return 0;
	}
}

static inline uint32_t dma2irq(enum mdp4_dma dma)
{
	switch (dma) {
	case DMA_P:    return MDP4_IRQ_DMA_P_DONE;
	case DMA_S:    return MDP4_IRQ_DMA_S_DONE;
	case DMA_E:    return MDP4_IRQ_DMA_E_DONE;
	default:       return 0;
	}
}

static inline uint32_t dma2err(enum mdp4_dma dma)
{
	switch (dma) {
	case DMA_P:    return MDP4_IRQ_PRIMARY_INTF_UDERRUN;
	case DMA_S:    return 0;  // ???
	case DMA_E:    return MDP4_IRQ_EXTERNAL_INTF_UDERRUN;
	default:       return 0;
	}
}

static inline uint32_t mixercfg(uint32_t mixer_cfg, int mixer,
		enum mdp4_pipe pipe, enum mdp_mixer_stage_id stage)
{
	switch (pipe) {
	case VG1:
		mixer_cfg &= ~(MDP4_LAYERMIXER_IN_CFG_PIPE0__MASK |
				MDP4_LAYERMIXER_IN_CFG_PIPE0_MIXER1);
		mixer_cfg |= MDP4_LAYERMIXER_IN_CFG_PIPE0(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE0_MIXER1);
		break;
	case VG2:
		mixer_cfg &= ~(MDP4_LAYERMIXER_IN_CFG_PIPE1__MASK |
				MDP4_LAYERMIXER_IN_CFG_PIPE1_MIXER1);
		mixer_cfg |= MDP4_LAYERMIXER_IN_CFG_PIPE1(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE1_MIXER1);
		break;
	case RGB1:
		mixer_cfg &= ~(MDP4_LAYERMIXER_IN_CFG_PIPE2__MASK |
				MDP4_LAYERMIXER_IN_CFG_PIPE2_MIXER1);
		mixer_cfg |= MDP4_LAYERMIXER_IN_CFG_PIPE2(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE2_MIXER1);
		break;
	case RGB2:
		mixer_cfg &= ~(MDP4_LAYERMIXER_IN_CFG_PIPE3__MASK |
				MDP4_LAYERMIXER_IN_CFG_PIPE3_MIXER1);
		mixer_cfg |= MDP4_LAYERMIXER_IN_CFG_PIPE3(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE3_MIXER1);
		break;
	case RGB3:
		mixer_cfg &= ~(MDP4_LAYERMIXER_IN_CFG_PIPE4__MASK |
				MDP4_LAYERMIXER_IN_CFG_PIPE4_MIXER1);
		mixer_cfg |= MDP4_LAYERMIXER_IN_CFG_PIPE4(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE4_MIXER1);
		break;
	case VG3:
		mixer_cfg &= ~(MDP4_LAYERMIXER_IN_CFG_PIPE5__MASK |
				MDP4_LAYERMIXER_IN_CFG_PIPE5_MIXER1);
		mixer_cfg |= MDP4_LAYERMIXER_IN_CFG_PIPE5(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE5_MIXER1);
		break;
	case VG4:
		mixer_cfg &= ~(MDP4_LAYERMIXER_IN_CFG_PIPE6__MASK |
				MDP4_LAYERMIXER_IN_CFG_PIPE6_MIXER1);
		mixer_cfg |= MDP4_LAYERMIXER_IN_CFG_PIPE6(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE6_MIXER1);
		break;
	default:
		WARN(1, "invalid pipe");
		break;
	}

	return mixer_cfg;
}

int mdp4_disable(struct mdp4_kms *mdp4_kms);
int mdp4_enable(struct mdp4_kms *mdp4_kms);

void mdp4_set_irqmask(struct mdp_kms *mdp_kms, uint32_t irqmask,
		uint32_t old_irqmask);
void mdp4_irq_preinstall(struct msm_kms *kms);
int mdp4_irq_postinstall(struct msm_kms *kms);
void mdp4_irq_uninstall(struct msm_kms *kms);
irqreturn_t mdp4_irq(struct msm_kms *kms);
int mdp4_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);
void mdp4_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);

static inline uint32_t mdp4_pipe_caps(enum mdp4_pipe pipe)
{
	switch (pipe) {
	case VG1:
	case VG2:
	case VG3:
	case VG4:
		return MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_CSC;
	case RGB1:
	case RGB2:
	case RGB3:
		return MDP_PIPE_CAP_SCALE;
	default:
		return 0;
	}
}

enum mdp4_pipe mdp4_plane_pipe(struct drm_plane *plane);
struct drm_plane *mdp4_plane_init(struct drm_device *dev,
		enum mdp4_pipe pipe_id, bool private_plane);

uint32_t mdp4_crtc_vblank(struct drm_crtc *crtc);
void mdp4_crtc_set_config(struct drm_crtc *crtc, uint32_t config);
void mdp4_crtc_set_intf(struct drm_crtc *crtc, enum mdp4_intf intf, int mixer);
void mdp4_crtc_wait_for_commit_done(struct drm_crtc *crtc);
struct drm_crtc *mdp4_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, int id, int ovlp_id,
		enum mdp4_dma dma_id);

long mdp4_dtv_round_pixclk(struct drm_encoder *encoder, unsigned long rate);
struct drm_encoder *mdp4_dtv_encoder_init(struct drm_device *dev);

long mdp4_lcdc_round_pixclk(struct drm_encoder *encoder, unsigned long rate);
struct drm_encoder *mdp4_lcdc_encoder_init(struct drm_device *dev,
		struct device_node *panel_node);

struct drm_connector *mdp4_lvds_connector_init(struct drm_device *dev,
		struct device_node *panel_node, struct drm_encoder *encoder);

#ifdef CONFIG_DRM_MSM_DSI
struct drm_encoder *mdp4_dsi_encoder_init(struct drm_device *dev);
#else
static inline struct drm_encoder *mdp4_dsi_encoder_init(struct drm_device *dev)
{
	return ERR_PTR(-ENODEV);
}
#endif

#ifdef CONFIG_COMMON_CLK
struct clk *mpd4_lvds_pll_init(struct drm_device *dev);
#else
static inline struct clk *mpd4_lvds_pll_init(struct drm_device *dev)
{
	return ERR_PTR(-ENODEV);
}
#endif

#ifdef DOWNSTREAM_CONFIG_MSM_BUS_SCALING
/* bus scaling data is associated with extra pointless platform devices,
 * "dtv", etc.. this is a bit of a hack, but we need a way for encoders
 * to find their pdata to make the bus-scaling stuff work.
 */
static inline void *mdp4_find_pdata(const char *devname)
{
	struct device *dev;
	dev = bus_find_device_by_name(&platform_bus_type, NULL, devname);
	return dev ? dev->platform_data : NULL;
}
#endif

#endif /* __MDP4_KMS_H__ */
