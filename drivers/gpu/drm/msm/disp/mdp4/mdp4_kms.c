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


#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "mdp4_kms.h"

static struct mdp4_platform_config *mdp4_get_config(struct platform_device *dev);

static int mdp4_hw_init(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	struct drm_device *dev = mdp4_kms->dev;
	uint32_t version, major, minor, dmap_cfg, vg_cfg;
	unsigned long clk;
	int ret = 0;

	pm_runtime_get_sync(dev->dev);

	mdp4_enable(mdp4_kms);
	version = mdp4_read(mdp4_kms, REG_MDP4_VERSION);
	mdp4_disable(mdp4_kms);

	major = FIELD(version, MDP4_VERSION_MAJOR);
	minor = FIELD(version, MDP4_VERSION_MINOR);

	DBG("found MDP4 version v%d.%d", major, minor);

	if (major != 4) {
		dev_err(dev->dev, "unexpected MDP version: v%d.%d\n",
				major, minor);
		ret = -ENXIO;
		goto out;
	}

	mdp4_kms->rev = minor;

	if (mdp4_kms->rev > 1) {
		mdp4_write(mdp4_kms, REG_MDP4_CS_CONTROLLER0, 0x0707ffff);
		mdp4_write(mdp4_kms, REG_MDP4_CS_CONTROLLER1, 0x03073f3f);
	}

	mdp4_write(mdp4_kms, REG_MDP4_PORTMAP_MODE, 0x3);

	/* max read pending cmd config, 3 pending requests: */
	mdp4_write(mdp4_kms, REG_MDP4_READ_CNFG, 0x02222);

	clk = clk_get_rate(mdp4_kms->clk);

	if ((mdp4_kms->rev >= 1) || (clk >= 90000000)) {
		dmap_cfg = 0x47;     /* 16 bytes-burst x 8 req */
		vg_cfg = 0x47;       /* 16 bytes-burs x 8 req */
	} else {
		dmap_cfg = 0x27;     /* 8 bytes-burst x 8 req */
		vg_cfg = 0x43;       /* 16 bytes-burst x 4 req */
	}

	DBG("fetch config: dmap=%02x, vg=%02x", dmap_cfg, vg_cfg);

	mdp4_write(mdp4_kms, REG_MDP4_DMA_FETCH_CONFIG(DMA_P), dmap_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_FETCH_CONFIG(DMA_E), dmap_cfg);

	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(VG1), vg_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(VG2), vg_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(RGB1), vg_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(RGB2), vg_cfg);

	if (mdp4_kms->rev >= 2)
		mdp4_write(mdp4_kms, REG_MDP4_LAYERMIXER_IN_CFG_UPDATE_METHOD, 1);
	mdp4_write(mdp4_kms, REG_MDP4_LAYERMIXER_IN_CFG, 0);

	/* disable CSC matrix / YUV by default: */
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_OP_MODE(VG1), 0);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_OP_MODE(VG2), 0);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_P_OP_MODE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_S_OP_MODE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_CSC_CONFIG(1), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_CSC_CONFIG(2), 0);

	if (mdp4_kms->rev > 1)
		mdp4_write(mdp4_kms, REG_MDP4_RESET_STATUS, 1);

	dev->mode_config.allow_fb_modifiers = true;

out:
	pm_runtime_put_sync(dev->dev);

	return ret;
}

static void mdp4_prepare_commit(struct msm_kms *kms, struct drm_atomic_state *state)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	int i;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;

	mdp4_enable(mdp4_kms);

	/* see 119ecb7fd */
	for_each_new_crtc_in_state(state, crtc, crtc_state, i)
		drm_crtc_vblank_get(crtc);
}

static void mdp4_complete_commit(struct msm_kms *kms, struct drm_atomic_state *state)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	int i;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;

	/* see 119ecb7fd */
	for_each_new_crtc_in_state(state, crtc, crtc_state, i)
		drm_crtc_vblank_put(crtc);

	mdp4_disable(mdp4_kms);
}

static void mdp4_wait_for_crtc_commit_done(struct msm_kms *kms,
						struct drm_crtc *crtc)
{
	mdp4_crtc_wait_for_commit_done(crtc);
}

static long mdp4_round_pixclk(struct msm_kms *kms, unsigned long rate,
		struct drm_encoder *encoder)
{
	/* if we had >1 encoder, we'd need something more clever: */
	switch (encoder->encoder_type) {
	case DRM_MODE_ENCODER_TMDS:
		return mdp4_dtv_round_pixclk(encoder, rate);
	case DRM_MODE_ENCODER_LVDS:
	case DRM_MODE_ENCODER_DSI:
	default:
		return rate;
	}
}

static const char * const iommu_ports[] = {
	"mdp_port0_cb0", "mdp_port1_cb0",
};

static void mdp4_destroy(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	struct device *dev = mdp4_kms->dev->dev;
	struct msm_gem_address_space *aspace = kms->aspace;

	if (mdp4_kms->blank_cursor_iova)
		msm_gem_put_iova(mdp4_kms->blank_cursor_bo, kms->aspace);
	drm_gem_object_put_unlocked(mdp4_kms->blank_cursor_bo);

	if (aspace) {
		aspace->mmu->funcs->detach(aspace->mmu,
				iommu_ports, ARRAY_SIZE(iommu_ports));
		msm_gem_address_space_put(aspace);
	}

	if (mdp4_kms->rpm_enabled)
		pm_runtime_disable(dev);

	kfree(mdp4_kms);
}

static const struct mdp_kms_funcs kms_funcs = {
	.base = {
		.hw_init         = mdp4_hw_init,
		.irq_preinstall  = mdp4_irq_preinstall,
		.irq_postinstall = mdp4_irq_postinstall,
		.irq_uninstall   = mdp4_irq_uninstall,
		.irq             = mdp4_irq,
		.enable_vblank   = mdp4_enable_vblank,
		.disable_vblank  = mdp4_disable_vblank,
		.prepare_commit  = mdp4_prepare_commit,
		.complete_commit = mdp4_complete_commit,
		.wait_for_crtc_commit_done = mdp4_wait_for_crtc_commit_done,
		.get_format      = mdp_get_format,
		.round_pixclk    = mdp4_round_pixclk,
		.destroy         = mdp4_destroy,
	},
	.set_irqmask         = mdp4_set_irqmask,
};

int mdp4_disable(struct mdp4_kms *mdp4_kms)
{
	DBG("");

	clk_disable_unprepare(mdp4_kms->clk);
	if (mdp4_kms->pclk)
		clk_disable_unprepare(mdp4_kms->pclk);
	clk_disable_unprepare(mdp4_kms->lut_clk);
	if (mdp4_kms->axi_clk)
		clk_disable_unprepare(mdp4_kms->axi_clk);

	return 0;
}

int mdp4_enable(struct mdp4_kms *mdp4_kms)
{
	DBG("");

	clk_prepare_enable(mdp4_kms->clk);
	if (mdp4_kms->pclk)
		clk_prepare_enable(mdp4_kms->pclk);
	clk_prepare_enable(mdp4_kms->lut_clk);
	if (mdp4_kms->axi_clk)
		clk_prepare_enable(mdp4_kms->axi_clk);

	return 0;
}


static int mdp4_modeset_init_intf(struct mdp4_kms *mdp4_kms,
				  int intf_type)
{
	struct drm_device *dev = mdp4_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct device_node *panel_node;
	int dsi_id;
	int ret;

	switch (intf_type) {
	case DRM_MODE_ENCODER_LVDS:
		/*
		 * bail out early if there is no panel node (no need to
		 * initialize LCDC encoder and LVDS connector)
		 */
		panel_node = of_graph_get_remote_node(dev->dev->of_node, 0, 0);
		if (!panel_node)
			return 0;

		encoder = mdp4_lcdc_encoder_init(dev, panel_node);
		if (IS_ERR(encoder)) {
			dev_err(dev->dev, "failed to construct LCDC encoder\n");
			return PTR_ERR(encoder);
		}

		/* LCDC can be hooked to DMA_P (TODO: Add DMA_S later?) */
		encoder->possible_crtcs = 1 << DMA_P;

		connector = mdp4_lvds_connector_init(dev, panel_node, encoder);
		if (IS_ERR(connector)) {
			dev_err(dev->dev, "failed to initialize LVDS connector\n");
			return PTR_ERR(connector);
		}

		priv->encoders[priv->num_encoders++] = encoder;
		priv->connectors[priv->num_connectors++] = connector;

		break;
	case DRM_MODE_ENCODER_TMDS:
		encoder = mdp4_dtv_encoder_init(dev);
		if (IS_ERR(encoder)) {
			dev_err(dev->dev, "failed to construct DTV encoder\n");
			return PTR_ERR(encoder);
		}

		/* DTV can be hooked to DMA_E: */
		encoder->possible_crtcs = 1 << 1;

		if (priv->hdmi) {
			/* Construct bridge/connector for HDMI: */
			ret = msm_hdmi_modeset_init(priv->hdmi, dev, encoder);
			if (ret) {
				dev_err(dev->dev, "failed to initialize HDMI: %d\n", ret);
				return ret;
			}
		}

		priv->encoders[priv->num_encoders++] = encoder;

		break;
	case DRM_MODE_ENCODER_DSI:
		/* only DSI1 supported for now */
		dsi_id = 0;

		if (!priv->dsi[dsi_id])
			break;

		encoder = mdp4_dsi_encoder_init(dev);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			dev_err(dev->dev,
				"failed to construct DSI encoder: %d\n", ret);
			return ret;
		}

		/* TODO: Add DMA_S later? */
		encoder->possible_crtcs = 1 << DMA_P;
		priv->encoders[priv->num_encoders++] = encoder;

		ret = msm_dsi_modeset_init(priv->dsi[dsi_id], dev, encoder);
		if (ret) {
			dev_err(dev->dev, "failed to initialize DSI: %d\n",
				ret);
			return ret;
		}

		break;
	default:
		dev_err(dev->dev, "Invalid or unsupported interface\n");
		return -EINVAL;
	}

	return 0;
}

static int modeset_init(struct mdp4_kms *mdp4_kms)
{
	struct drm_device *dev = mdp4_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int i, ret;
	static const enum mdp4_pipe rgb_planes[] = {
		RGB1, RGB2,
	};
	static const enum mdp4_pipe vg_planes[] = {
		VG1, VG2,
	};
	static const enum mdp4_dma mdp4_crtcs[] = {
		DMA_P, DMA_E,
	};
	static const char * const mdp4_crtc_names[] = {
		"DMA_P", "DMA_E",
	};
	static const int mdp4_intfs[] = {
		DRM_MODE_ENCODER_LVDS,
		DRM_MODE_ENCODER_DSI,
		DRM_MODE_ENCODER_TMDS,
	};

	/* construct non-private planes: */
	for (i = 0; i < ARRAY_SIZE(vg_planes); i++) {
		plane = mdp4_plane_init(dev, vg_planes[i], false);
		if (IS_ERR(plane)) {
			dev_err(dev->dev,
				"failed to construct plane for VG%d\n", i + 1);
			ret = PTR_ERR(plane);
			goto fail;
		}
		priv->planes[priv->num_planes++] = plane;
	}

	for (i = 0; i < ARRAY_SIZE(mdp4_crtcs); i++) {
		plane = mdp4_plane_init(dev, rgb_planes[i], true);
		if (IS_ERR(plane)) {
			dev_err(dev->dev,
				"failed to construct plane for RGB%d\n", i + 1);
			ret = PTR_ERR(plane);
			goto fail;
		}

		crtc  = mdp4_crtc_init(dev, plane, priv->num_crtcs, i,
				mdp4_crtcs[i]);
		if (IS_ERR(crtc)) {
			dev_err(dev->dev, "failed to construct crtc for %s\n",
				mdp4_crtc_names[i]);
			ret = PTR_ERR(crtc);
			goto fail;
		}

		priv->crtcs[priv->num_crtcs++] = crtc;
	}

	/*
	 * we currently set up two relatively fixed paths:
	 *
	 * LCDC/LVDS path: RGB1 -> DMA_P -> LCDC -> LVDS
	 *			or
	 * DSI path: RGB1 -> DMA_P -> DSI1 -> DSI Panel
	 *
	 * DTV/HDMI path: RGB2 -> DMA_E -> DTV -> HDMI
	 */

	for (i = 0; i < ARRAY_SIZE(mdp4_intfs); i++) {
		ret = mdp4_modeset_init_intf(mdp4_kms, mdp4_intfs[i]);
		if (ret) {
			dev_err(dev->dev, "failed to initialize intf: %d, %d\n",
				i, ret);
			goto fail;
		}
	}

	return 0;

fail:
	return ret;
}

struct msm_kms *mdp4_kms_init(struct drm_device *dev)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct mdp4_platform_config *config = mdp4_get_config(pdev);
	struct mdp4_kms *mdp4_kms;
	struct msm_kms *kms = NULL;
	struct msm_gem_address_space *aspace;
	int irq, ret;

	mdp4_kms = kzalloc(sizeof(*mdp4_kms), GFP_KERNEL);
	if (!mdp4_kms) {
		dev_err(dev->dev, "failed to allocate kms\n");
		ret = -ENOMEM;
		goto fail;
	}

	mdp_kms_init(&mdp4_kms->base, &kms_funcs);

	kms = &mdp4_kms->base.base;

	mdp4_kms->dev = dev;

	mdp4_kms->mmio = msm_ioremap(pdev, NULL, "MDP4");
	if (IS_ERR(mdp4_kms->mmio)) {
		ret = PTR_ERR(mdp4_kms->mmio);
		goto fail;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		dev_err(dev->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	kms->irq = irq;

	/* NOTE: driver for this regulator still missing upstream.. use
	 * _get_exclusive() and ignore the error if it does not exist
	 * (and hope that the bootloader left it on for us)
	 */
	mdp4_kms->vdd = devm_regulator_get_exclusive(&pdev->dev, "vdd");
	if (IS_ERR(mdp4_kms->vdd))
		mdp4_kms->vdd = NULL;

	if (mdp4_kms->vdd) {
		ret = regulator_enable(mdp4_kms->vdd);
		if (ret) {
			dev_err(dev->dev, "failed to enable regulator vdd: %d\n", ret);
			goto fail;
		}
	}

	mdp4_kms->clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(mdp4_kms->clk)) {
		dev_err(dev->dev, "failed to get core_clk\n");
		ret = PTR_ERR(mdp4_kms->clk);
		goto fail;
	}

	mdp4_kms->pclk = devm_clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(mdp4_kms->pclk))
		mdp4_kms->pclk = NULL;

	// XXX if (rev >= MDP_REV_42) { ???
	mdp4_kms->lut_clk = devm_clk_get(&pdev->dev, "lut_clk");
	if (IS_ERR(mdp4_kms->lut_clk)) {
		dev_err(dev->dev, "failed to get lut_clk\n");
		ret = PTR_ERR(mdp4_kms->lut_clk);
		goto fail;
	}

	mdp4_kms->axi_clk = devm_clk_get(&pdev->dev, "bus_clk");
	if (IS_ERR(mdp4_kms->axi_clk)) {
		dev_err(dev->dev, "failed to get axi_clk\n");
		ret = PTR_ERR(mdp4_kms->axi_clk);
		goto fail;
	}

	clk_set_rate(mdp4_kms->clk, config->max_clk);
	clk_set_rate(mdp4_kms->lut_clk, config->max_clk);

	pm_runtime_enable(dev->dev);
	mdp4_kms->rpm_enabled = true;

	/* make sure things are off before attaching iommu (bootloader could
	 * have left things on, in which case we'll start getting faults if
	 * we don't disable):
	 */
	mdp4_enable(mdp4_kms);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_ENABLE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_ENABLE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_DSI_ENABLE, 0);
	mdp4_disable(mdp4_kms);
	mdelay(16);

	if (config->iommu) {
		aspace = msm_gem_address_space_create(&pdev->dev,
				config->iommu, "mdp4");
		if (IS_ERR(aspace)) {
			ret = PTR_ERR(aspace);
			goto fail;
		}

		kms->aspace = aspace;

		ret = aspace->mmu->funcs->attach(aspace->mmu, iommu_ports,
				ARRAY_SIZE(iommu_ports));
		if (ret)
			goto fail;
	} else {
		dev_info(dev->dev, "no iommu, fallback to phys "
				"contig buffers for scanout\n");
		aspace = NULL;
	}

	ret = modeset_init(mdp4_kms);
	if (ret) {
		dev_err(dev->dev, "modeset_init failed: %d\n", ret);
		goto fail;
	}

	mdp4_kms->blank_cursor_bo = msm_gem_new(dev, SZ_16K, MSM_BO_WC);
	if (IS_ERR(mdp4_kms->blank_cursor_bo)) {
		ret = PTR_ERR(mdp4_kms->blank_cursor_bo);
		dev_err(dev->dev, "could not allocate blank-cursor bo: %d\n", ret);
		mdp4_kms->blank_cursor_bo = NULL;
		goto fail;
	}

	ret = msm_gem_get_iova(mdp4_kms->blank_cursor_bo, kms->aspace,
			&mdp4_kms->blank_cursor_iova);
	if (ret) {
		dev_err(dev->dev, "could not pin blank-cursor bo: %d\n", ret);
		goto fail;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	return kms;

fail:
	if (kms)
		mdp4_destroy(kms);
	return ERR_PTR(ret);
}

static struct mdp4_platform_config *mdp4_get_config(struct platform_device *dev)
{
	static struct mdp4_platform_config config = {};

	/* TODO: Chips that aren't apq8064 have a 200 Mhz max_clk */
	config.max_clk = 266667000;
	config.iommu = iommu_domain_alloc(&platform_bus_type);
	if (config.iommu) {
		config.iommu->geometry.aperture_start = 0x1000;
		config.iommu->geometry.aperture_end = 0xffffffff;
	}

	return &config;
}
