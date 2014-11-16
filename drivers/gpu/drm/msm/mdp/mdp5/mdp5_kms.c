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
#include "msm_mmu.h"
#include "mdp5_kms.h"

static const char *iommu_ports[] = {
		"mdp_0",
};

static struct mdp5_platform_config *mdp5_get_config(struct platform_device *dev);

const struct mdp5_config *mdp5_cfg;

static const struct mdp5_config msm8x74_config = {
	.name = "msm8x74",
	.ctl = {
		.count = 5,
		.base = { 0x00600, 0x00700, 0x00800, 0x00900, 0x00a00 },
	},
	.pipe_vig = {
		.count = 3,
		.base = { 0x01200, 0x01600, 0x01a00 },
	},
	.pipe_rgb = {
		.count = 3,
		.base = { 0x01e00, 0x02200, 0x02600 },
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x02a00, 0x02e00 },
	},
	.lm = {
		.count = 5,
		.base = { 0x03200, 0x03600, 0x03a00, 0x03e00, 0x04200 },
	},
	.dspp = {
		.count = 3,
		.base = { 0x04600, 0x04a00, 0x04e00 },
	},
	.ad = {
		.count = 2,
		.base = { 0x13100, 0x13300 }, /* NOTE: no ad in v1.0 */
	},
	.intf = {
		.count = 4,
		.base = { 0x12500, 0x12700, 0x12900, 0x12b00 },
	},
};

static const struct mdp5_config apq8084_config = {
	.name = "apq8084",
	.ctl = {
		.count = 5,
		.base = { 0x00600, 0x00700, 0x00800, 0x00900, 0x00a00 },
	},
	.pipe_vig = {
		.count = 4,
		.base = { 0x01200, 0x01600, 0x01a00, 0x01e00 },
	},
	.pipe_rgb = {
		.count = 4,
		.base = { 0x02200, 0x02600, 0x02a00, 0x02e00 },
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x03200, 0x03600 },
	},
	.lm = {
		.count = 6,
		.base = { 0x03a00, 0x03e00, 0x04200, 0x04600, 0x04a00, 0x04e00 },
	},
	.dspp = {
		.count = 4,
		.base = { 0x05200, 0x05600, 0x05a00, 0x05e00 },

	},
	.ad = {
		.count = 3,
		.base = { 0x13500, 0x13700, 0x13900 },
	},
	.intf = {
		.count = 5,
		.base = { 0x12500, 0x12700, 0x12900, 0x12b00, 0x12d00 },
	},
};

struct mdp5_config_entry {
	int revision;
	const struct mdp5_config *config;
};

static const struct mdp5_config_entry mdp5_configs[] = {
	{ .revision = 0, .config = &msm8x74_config },
	{ .revision = 2, .config = &msm8x74_config },
	{ .revision = 3, .config = &apq8084_config },
};

static int mdp5_select_hw_cfg(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct drm_device *dev = mdp5_kms->dev;
	uint32_t version, major, minor;
	int i, ret = 0;

	mdp5_enable(mdp5_kms);
	version = mdp5_read(mdp5_kms, REG_MDP5_MDP_VERSION);
	mdp5_disable(mdp5_kms);

	major = FIELD(version, MDP5_MDP_VERSION_MAJOR);
	minor = FIELD(version, MDP5_MDP_VERSION_MINOR);

	DBG("found MDP5 version v%d.%d", major, minor);

	if (major != 1) {
		dev_err(dev->dev, "unexpected MDP major version: v%d.%d\n",
				major, minor);
		ret = -ENXIO;
		goto out;
	}

	mdp5_kms->rev = minor;

	/* only after mdp5_cfg global pointer's init can we access the hw */
	for (i = 0; i < ARRAY_SIZE(mdp5_configs); i++) {
		if (mdp5_configs[i].revision != minor)
			continue;
		mdp5_kms->hw_cfg = mdp5_cfg = mdp5_configs[i].config;
		break;
	}
	if (unlikely(!mdp5_kms->hw_cfg)) {
		dev_err(dev->dev, "unexpected MDP minor revision: v%d.%d\n",
				major, minor);
		ret = -ENXIO;
		goto out;
	}

	DBG("MDP5: %s config selected", mdp5_kms->hw_cfg->name);

	return 0;
out:
	return ret;
}

static int mdp5_hw_init(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct drm_device *dev = mdp5_kms->dev;
	int i;

	pm_runtime_get_sync(dev->dev);

	/* Magic unknown register writes:
	 *
	 *    W VBIF:0x004 00000001      (mdss_mdp.c:839)
	 *    W MDP5:0x2e0 0xe9          (mdss_mdp.c:839)
	 *    W MDP5:0x2e4 0x55          (mdss_mdp.c:839)
	 *    W MDP5:0x3ac 0xc0000ccc    (mdss_mdp.c:839)
	 *    W MDP5:0x3b4 0xc0000ccc    (mdss_mdp.c:839)
	 *    W MDP5:0x3bc 0xcccccc      (mdss_mdp.c:839)
	 *    W MDP5:0x4a8 0xcccc0c0     (mdss_mdp.c:839)
	 *    W MDP5:0x4b0 0xccccc0c0    (mdss_mdp.c:839)
	 *    W MDP5:0x4b8 0xccccc000    (mdss_mdp.c:839)
	 *
	 * Downstream fbdev driver gets these register offsets/values
	 * from DT.. not really sure what these registers are or if
	 * different values for different boards/SoC's, etc.  I guess
	 * they are the golden registers.
	 *
	 * Not setting these does not seem to cause any problem.  But
	 * we may be getting lucky with the bootloader initializing
	 * them for us.  OTOH, if we can always count on the bootloader
	 * setting the golden registers, then perhaps we don't need to
	 * care.
	 */

	mdp5_write(mdp5_kms, REG_MDP5_DISP_INTF_SEL, 0);

	for (i = 0; i < mdp5_kms->hw_cfg->ctl.count; i++)
		mdp5_write(mdp5_kms, REG_MDP5_CTL_OP(i), 0);

	pm_runtime_put_sync(dev->dev);

	return 0;
}

static long mdp5_round_pixclk(struct msm_kms *kms, unsigned long rate,
		struct drm_encoder *encoder)
{
	return rate;
}

static void mdp5_preclose(struct msm_kms *kms, struct drm_file *file)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct msm_drm_private *priv = mdp5_kms->dev->dev_private;
	unsigned i;

	for (i = 0; i < priv->num_crtcs; i++)
		mdp5_crtc_cancel_pending_flip(priv->crtcs[i], file);
}

static void mdp5_destroy(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct msm_mmu *mmu = mdp5_kms->mmu;

	if (mmu) {
		mmu->funcs->detach(mmu, iommu_ports, ARRAY_SIZE(iommu_ports));
		mmu->funcs->destroy(mmu);
	}
	kfree(mdp5_kms);
}

static const struct mdp_kms_funcs kms_funcs = {
	.base = {
		.hw_init         = mdp5_hw_init,
		.irq_preinstall  = mdp5_irq_preinstall,
		.irq_postinstall = mdp5_irq_postinstall,
		.irq_uninstall   = mdp5_irq_uninstall,
		.irq             = mdp5_irq,
		.enable_vblank   = mdp5_enable_vblank,
		.disable_vblank  = mdp5_disable_vblank,
		.get_format      = mdp_get_format,
		.round_pixclk    = mdp5_round_pixclk,
		.preclose        = mdp5_preclose,
		.destroy         = mdp5_destroy,
	},
	.set_irqmask         = mdp5_set_irqmask,
};

int mdp5_disable(struct mdp5_kms *mdp5_kms)
{
	DBG("");

	clk_disable_unprepare(mdp5_kms->ahb_clk);
	clk_disable_unprepare(mdp5_kms->axi_clk);
	clk_disable_unprepare(mdp5_kms->core_clk);
	clk_disable_unprepare(mdp5_kms->lut_clk);

	return 0;
}

int mdp5_enable(struct mdp5_kms *mdp5_kms)
{
	DBG("");

	clk_prepare_enable(mdp5_kms->ahb_clk);
	clk_prepare_enable(mdp5_kms->axi_clk);
	clk_prepare_enable(mdp5_kms->core_clk);
	clk_prepare_enable(mdp5_kms->lut_clk);

	return 0;
}

static int modeset_init(struct mdp5_kms *mdp5_kms)
{
	static const enum mdp5_pipe crtcs[] = {
			SSPP_RGB0, SSPP_RGB1, SSPP_RGB2, SSPP_RGB3,
	};
	struct drm_device *dev = mdp5_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder;
	int i, ret;

	/* construct CRTCs: */
	for (i = 0; i < mdp5_kms->hw_cfg->pipe_rgb.count; i++) {
		struct drm_plane *plane;
		struct drm_crtc *crtc;

		plane = mdp5_plane_init(dev, crtcs[i], true);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			dev_err(dev->dev, "failed to construct plane for %s (%d)\n",
					pipe2name(crtcs[i]), ret);
			goto fail;
		}

		crtc  = mdp5_crtc_init(dev, plane, i);
		if (IS_ERR(crtc)) {
			ret = PTR_ERR(crtc);
			dev_err(dev->dev, "failed to construct crtc for %s (%d)\n",
					pipe2name(crtcs[i]), ret);
			goto fail;
		}
		priv->crtcs[priv->num_crtcs++] = crtc;
	}

	/* Construct encoder for HDMI: */
	encoder = mdp5_encoder_init(dev, 3, INTF_HDMI);
	if (IS_ERR(encoder)) {
		dev_err(dev->dev, "failed to construct encoder\n");
		ret = PTR_ERR(encoder);
		goto fail;
	}

	/* NOTE: the vsync and error irq's are actually associated with
	 * the INTF/encoder.. the easiest way to deal with this (ie. what
	 * we do now) is assume a fixed relationship between crtc's and
	 * encoders.  I'm not sure if there is ever a need to more freely
	 * assign crtcs to encoders, but if there is then we need to take
	 * care of error and vblank irq's that the crtc has registered,
	 * and also update user-requested vblank_mask.
	 */
	encoder->possible_crtcs = BIT(0);
	mdp5_crtc_set_intf(priv->crtcs[0], 3, INTF_HDMI);

	priv->encoders[priv->num_encoders++] = encoder;

	/* Construct bridge/connector for HDMI: */
	if (priv->hdmi) {
		ret = hdmi_modeset_init(priv->hdmi, dev, encoder);
		if (ret) {
			dev_err(dev->dev, "failed to initialize HDMI: %d\n", ret);
			goto fail;
		}
	}

	return 0;

fail:
	return ret;
}

static int get_clk(struct platform_device *pdev, struct clk **clkp,
		const char *name)
{
	struct device *dev = &pdev->dev;
	struct clk *clk = devm_clk_get(dev, name);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get %s (%ld)\n", name, PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	*clkp = clk;
	return 0;
}

struct msm_kms *mdp5_kms_init(struct drm_device *dev)
{
	struct platform_device *pdev = dev->platformdev;
	struct mdp5_platform_config *config = mdp5_get_config(pdev);
	struct mdp5_kms *mdp5_kms;
	struct msm_kms *kms = NULL;
	struct msm_mmu *mmu;
	int i, ret;

	mdp5_kms = kzalloc(sizeof(*mdp5_kms), GFP_KERNEL);
	if (!mdp5_kms) {
		dev_err(dev->dev, "failed to allocate kms\n");
		ret = -ENOMEM;
		goto fail;
	}

	mdp_kms_init(&mdp5_kms->base, &kms_funcs);

	kms = &mdp5_kms->base.base;

	mdp5_kms->dev = dev;
	mdp5_kms->smp_blk_cnt = config->smp_blk_cnt;

	mdp5_kms->mmio = msm_ioremap(pdev, "mdp_phys", "MDP5");
	if (IS_ERR(mdp5_kms->mmio)) {
		ret = PTR_ERR(mdp5_kms->mmio);
		goto fail;
	}

	mdp5_kms->vbif = msm_ioremap(pdev, "vbif_phys", "VBIF");
	if (IS_ERR(mdp5_kms->vbif)) {
		ret = PTR_ERR(mdp5_kms->vbif);
		goto fail;
	}

	mdp5_kms->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(mdp5_kms->vdd)) {
		ret = PTR_ERR(mdp5_kms->vdd);
		goto fail;
	}

	ret = regulator_enable(mdp5_kms->vdd);
	if (ret) {
		dev_err(dev->dev, "failed to enable regulator vdd: %d\n", ret);
		goto fail;
	}

	ret = get_clk(pdev, &mdp5_kms->axi_clk, "bus_clk");
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->ahb_clk, "iface_clk");
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->src_clk, "core_clk_src");
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->core_clk, "core_clk");
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->lut_clk, "lut_clk");
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->vsync_clk, "vsync_clk");
	if (ret)
		goto fail;

	ret = clk_set_rate(mdp5_kms->src_clk, config->max_clk);

	ret = mdp5_select_hw_cfg(kms);
	if (ret)
		goto fail;

	/* make sure things are off before attaching iommu (bootloader could
	 * have left things on, in which case we'll start getting faults if
	 * we don't disable):
	 */
	mdp5_enable(mdp5_kms);
	for (i = 0; i < mdp5_kms->hw_cfg->intf.count; i++)
		mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(i), 0);
	mdp5_disable(mdp5_kms);
	mdelay(16);

	if (config->iommu) {
		mmu = msm_iommu_new(&pdev->dev, config->iommu);
		if (IS_ERR(mmu)) {
			ret = PTR_ERR(mmu);
			dev_err(dev->dev, "failed to init iommu: %d\n", ret);
			goto fail;
		}

		ret = mmu->funcs->attach(mmu, iommu_ports,
				ARRAY_SIZE(iommu_ports));
		if (ret) {
			dev_err(dev->dev, "failed to attach iommu: %d\n", ret);
			mmu->funcs->destroy(mmu);
			goto fail;
		}
	} else {
		dev_info(dev->dev, "no iommu, fallback to phys "
				"contig buffers for scanout\n");
		mmu = NULL;
	}
	mdp5_kms->mmu = mmu;

	mdp5_kms->id = msm_register_mmu(dev, mmu);
	if (mdp5_kms->id < 0) {
		ret = mdp5_kms->id;
		dev_err(dev->dev, "failed to register mdp5 iommu: %d\n", ret);
		goto fail;
	}

	ret = modeset_init(mdp5_kms);
	if (ret) {
		dev_err(dev->dev, "modeset_init failed: %d\n", ret);
		goto fail;
	}

	return kms;

fail:
	if (kms)
		mdp5_destroy(kms);
	return ERR_PTR(ret);
}

static struct mdp5_platform_config *mdp5_get_config(struct platform_device *dev)
{
	static struct mdp5_platform_config config = {};
#ifdef CONFIG_OF
	/* TODO */
#endif
	config.iommu = iommu_domain_alloc(&platform_bus_type);
	/* TODO hard-coded in downstream mdss, but should it be? */
	config.max_clk = 200000000;
	/* TODO get from DT: */
	config.smp_blk_cnt = 22;

	return &config;
}
