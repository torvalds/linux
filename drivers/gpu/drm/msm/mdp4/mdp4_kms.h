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

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "msm_drv.h"
#include "mdp4.xml.h"


/* For transiently registering for different MDP4 irqs that various parts
 * of the KMS code need during setup/configuration.  We these are not
 * necessarily the same as what drm_vblank_get/put() are requesting, and
 * the hysteresis in drm_vblank_put() is not necessarily desirable for
 * internal housekeeping related irq usage.
 */
struct mdp4_irq {
	struct list_head node;
	uint32_t irqmask;
	bool registered;
	void (*irq)(struct mdp4_irq *irq, uint32_t irqstatus);
};

struct mdp4_kms {
	struct msm_kms base;

	struct drm_device *dev;

	int rev;

	/* mapper-id used to request GEM buffer mapped for scanout: */
	int id;

	void __iomem *mmio;

	struct regulator *dsi_pll_vdda;
	struct regulator *dsi_pll_vddio;
	struct regulator *vdd;

	struct clk *clk;
	struct clk *pclk;
	struct clk *lut_clk;

	/* irq handling: */
	bool in_irq;
	struct list_head irq_list;    /* list of mdp4_irq */
	uint32_t vblank_mask;         /* irq bits set for userspace vblank */
	struct mdp4_irq error_handler;
};
#define to_mdp4_kms(x) container_of(x, struct mdp4_kms, base)

/* platform config data (ie. from DT, or pdata) */
struct mdp4_platform_config {
	struct iommu_domain *iommu;
	uint32_t max_clk;
};

struct mdp4_format {
	struct msm_format base;
	enum mdp4_bpc bpc_r, bpc_g, bpc_b;
	enum mdp4_bpc_alpha bpc_a;
	uint8_t unpack[4];
	bool alpha_enable, unpack_tight;
	uint8_t cpp, unpack_count;
};
#define to_mdp4_format(x) container_of(x, struct mdp4_format, base)

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
	case RGB2:     return MDP4_OVERLAY_FLUSH_RGB1;
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

static inline uint32_t mixercfg(int mixer, enum mdp4_pipe pipe,
		enum mdp4_mixer_stage_id stage)
{
	uint32_t mixer_cfg = 0;

	switch (pipe) {
	case VG1:
		mixer_cfg = MDP4_LAYERMIXER_IN_CFG_PIPE0(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE0_MIXER1);
		break;
	case VG2:
		mixer_cfg = MDP4_LAYERMIXER_IN_CFG_PIPE1(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE1_MIXER1);
		break;
	case RGB1:
		mixer_cfg = MDP4_LAYERMIXER_IN_CFG_PIPE2(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE2_MIXER1);
		break;
	case RGB2:
		mixer_cfg = MDP4_LAYERMIXER_IN_CFG_PIPE3(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE3_MIXER1);
		break;
	case RGB3:
		mixer_cfg = MDP4_LAYERMIXER_IN_CFG_PIPE4(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE4_MIXER1);
		break;
	case VG3:
		mixer_cfg = MDP4_LAYERMIXER_IN_CFG_PIPE5(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE5_MIXER1);
		break;
	case VG4:
		mixer_cfg = MDP4_LAYERMIXER_IN_CFG_PIPE6(stage) |
			COND(mixer == 1, MDP4_LAYERMIXER_IN_CFG_PIPE6_MIXER1);
		break;
	default:
		WARN_ON("invalid pipe");
		break;
	}

	return mixer_cfg;
}

int mdp4_disable(struct mdp4_kms *mdp4_kms);
int mdp4_enable(struct mdp4_kms *mdp4_kms);

void mdp4_irq_preinstall(struct msm_kms *kms);
int mdp4_irq_postinstall(struct msm_kms *kms);
void mdp4_irq_uninstall(struct msm_kms *kms);
irqreturn_t mdp4_irq(struct msm_kms *kms);
void mdp4_irq_wait(struct mdp4_kms *mdp4_kms, uint32_t irqmask);
void mdp4_irq_register(struct mdp4_kms *mdp4_kms, struct mdp4_irq *irq);
void mdp4_irq_unregister(struct mdp4_kms *mdp4_kms, struct mdp4_irq *irq);
int mdp4_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);
void mdp4_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);

uint32_t mdp4_get_formats(enum mdp4_pipe pipe_id, uint32_t *formats,
		uint32_t max_formats);
const struct msm_format *mdp4_get_format(struct msm_kms *kms, uint32_t format);

void mdp4_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj);
void mdp4_plane_set_scanout(struct drm_plane *plane,
		struct drm_framebuffer *fb);
int mdp4_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h);
enum mdp4_pipe mdp4_plane_pipe(struct drm_plane *plane);
struct drm_plane *mdp4_plane_init(struct drm_device *dev,
		enum mdp4_pipe pipe_id, bool private_plane);

uint32_t mdp4_crtc_vblank(struct drm_crtc *crtc);
void mdp4_crtc_cancel_pending_flip(struct drm_crtc *crtc, struct drm_file *file);
void mdp4_crtc_set_config(struct drm_crtc *crtc, uint32_t config);
void mdp4_crtc_set_intf(struct drm_crtc *crtc, enum mdp4_intf intf);
void mdp4_crtc_attach(struct drm_crtc *crtc, struct drm_plane *plane);
void mdp4_crtc_detach(struct drm_crtc *crtc, struct drm_plane *plane);
struct drm_crtc *mdp4_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, int id, int ovlp_id,
		enum mdp4_dma dma_id);

long mdp4_dtv_round_pixclk(struct drm_encoder *encoder, unsigned long rate);
struct drm_encoder *mdp4_dtv_encoder_init(struct drm_device *dev);

#ifdef CONFIG_MSM_BUS_SCALING
static inline int match_dev_name(struct device *dev, void *data)
{
	return !strcmp(dev_name(dev), data);
}
/* bus scaling data is associated with extra pointless platform devices,
 * "dtv", etc.. this is a bit of a hack, but we need a way for encoders
 * to find their pdata to make the bus-scaling stuff work.
 */
static inline void *mdp4_find_pdata(const char *devname)
{
	struct device *dev;
	dev = bus_find_device(&platform_bus_type, NULL,
			(void *)devname, match_dev_name);
	return dev ? dev->platform_data : NULL;
}
#endif

#endif /* __MDP4_KMS_H__ */
