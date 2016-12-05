/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

#include <linux/of_irq.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "mdp5_kms.h"

static const char *iommu_ports[] = {
		"mdp_0",
};

static int mdp5_hw_init(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct platform_device *pdev = mdp5_kms->pdev;
	unsigned long flags;

	pm_runtime_get_sync(&pdev->dev);
	mdp5_enable(mdp5_kms);

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

	spin_lock_irqsave(&mdp5_kms->resource_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_DISP_INTF_SEL, 0);
	spin_unlock_irqrestore(&mdp5_kms->resource_lock, flags);

	mdp5_ctlm_hw_reset(mdp5_kms->ctlm);

	mdp5_disable(mdp5_kms);
	pm_runtime_put_sync(&pdev->dev);

	return 0;
}

struct mdp5_state *mdp5_get_state(struct drm_atomic_state *s)
{
	struct msm_drm_private *priv = s->dev->dev_private;
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(priv->kms));
	struct msm_kms_state *state = to_kms_state(s);
	struct mdp5_state *new_state;
	int ret;

	if (state->state)
		return state->state;

	ret = drm_modeset_lock(&mdp5_kms->state_lock, s->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	new_state = kmalloc(sizeof(*mdp5_kms->state), GFP_KERNEL);
	if (!new_state)
		return ERR_PTR(-ENOMEM);

	/* Copy state: */
	new_state->hwpipe = mdp5_kms->state->hwpipe;
	if (mdp5_kms->smp)
		new_state->smp = mdp5_kms->state->smp;

	state->state = new_state;

	return new_state;
}

static void mdp5_swap_state(struct msm_kms *kms, struct drm_atomic_state *state)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	swap(to_kms_state(state)->state, mdp5_kms->state);
}

static void mdp5_prepare_commit(struct msm_kms *kms, struct drm_atomic_state *state)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));

	mdp5_enable(mdp5_kms);

	if (mdp5_kms->smp)
		mdp5_smp_prepare_commit(mdp5_kms->smp, &mdp5_kms->state->smp);
}

static void mdp5_complete_commit(struct msm_kms *kms, struct drm_atomic_state *state)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));

	if (mdp5_kms->smp)
		mdp5_smp_complete_commit(mdp5_kms->smp, &mdp5_kms->state->smp);

	mdp5_disable(mdp5_kms);
}

static void mdp5_wait_for_crtc_commit_done(struct msm_kms *kms,
						struct drm_crtc *crtc)
{
	mdp5_crtc_wait_for_commit_done(crtc);
}

static long mdp5_round_pixclk(struct msm_kms *kms, unsigned long rate,
		struct drm_encoder *encoder)
{
	return rate;
}

static int mdp5_set_split_display(struct msm_kms *kms,
		struct drm_encoder *encoder,
		struct drm_encoder *slave_encoder,
		bool is_cmd_mode)
{
	if (is_cmd_mode)
		return mdp5_cmd_encoder_set_split_display(encoder,
							slave_encoder);
	else
		return mdp5_encoder_set_split_display(encoder, slave_encoder);
}

static void mdp5_set_encoder_mode(struct msm_kms *kms,
				  struct drm_encoder *encoder,
				  bool cmd_mode)
{
	mdp5_encoder_set_intf_mode(encoder, cmd_mode);
}

static void mdp5_kms_destroy(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct msm_gem_address_space *aspace = mdp5_kms->aspace;
	int i;

	for (i = 0; i < mdp5_kms->num_hwpipes; i++)
		mdp5_pipe_destroy(mdp5_kms->hwpipes[i]);

	if (aspace) {
		aspace->mmu->funcs->detach(aspace->mmu,
				iommu_ports, ARRAY_SIZE(iommu_ports));
		msm_gem_address_space_destroy(aspace);
	}
}

#ifdef CONFIG_DEBUG_FS
static int smp_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(priv->kms));
	struct drm_printer p = drm_seq_file_printer(m);

	if (!mdp5_kms->smp) {
		drm_printf(&p, "no SMP pool\n");
		return 0;
	}

	mdp5_smp_dump(mdp5_kms->smp, &p);

	return 0;
}

static struct drm_info_list mdp5_debugfs_list[] = {
		{"smp", smp_show },
};

static int mdp5_kms_debugfs_init(struct msm_kms *kms, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	ret = drm_debugfs_create_files(mdp5_debugfs_list,
			ARRAY_SIZE(mdp5_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install mdp5_debugfs_list\n");
		return ret;
	}

	return 0;
}

static void mdp5_kms_debugfs_cleanup(struct msm_kms *kms, struct drm_minor *minor)
{
	drm_debugfs_remove_files(mdp5_debugfs_list,
			ARRAY_SIZE(mdp5_debugfs_list), minor);
}
#endif

static const struct mdp_kms_funcs kms_funcs = {
	.base = {
		.hw_init         = mdp5_hw_init,
		.irq_preinstall  = mdp5_irq_preinstall,
		.irq_postinstall = mdp5_irq_postinstall,
		.irq_uninstall   = mdp5_irq_uninstall,
		.irq             = mdp5_irq,
		.enable_vblank   = mdp5_enable_vblank,
		.disable_vblank  = mdp5_disable_vblank,
		.swap_state      = mdp5_swap_state,
		.prepare_commit  = mdp5_prepare_commit,
		.complete_commit = mdp5_complete_commit,
		.wait_for_crtc_commit_done = mdp5_wait_for_crtc_commit_done,
		.get_format      = mdp_get_format,
		.round_pixclk    = mdp5_round_pixclk,
		.set_split_display = mdp5_set_split_display,
		.set_encoder_mode = mdp5_set_encoder_mode,
		.destroy         = mdp5_kms_destroy,
#ifdef CONFIG_DEBUG_FS
		.debugfs_init    = mdp5_kms_debugfs_init,
		.debugfs_cleanup = mdp5_kms_debugfs_cleanup,
#endif
	},
	.set_irqmask         = mdp5_set_irqmask,
};

int mdp5_disable(struct mdp5_kms *mdp5_kms)
{
	DBG("");

	clk_disable_unprepare(mdp5_kms->ahb_clk);
	clk_disable_unprepare(mdp5_kms->axi_clk);
	clk_disable_unprepare(mdp5_kms->core_clk);
	if (mdp5_kms->lut_clk)
		clk_disable_unprepare(mdp5_kms->lut_clk);

	return 0;
}

int mdp5_enable(struct mdp5_kms *mdp5_kms)
{
	DBG("");

	clk_prepare_enable(mdp5_kms->ahb_clk);
	clk_prepare_enable(mdp5_kms->axi_clk);
	clk_prepare_enable(mdp5_kms->core_clk);
	if (mdp5_kms->lut_clk)
		clk_prepare_enable(mdp5_kms->lut_clk);

	return 0;
}

static struct drm_encoder *construct_encoder(struct mdp5_kms *mdp5_kms,
		enum mdp5_intf_type intf_type, int intf_num,
		enum mdp5_intf_mode intf_mode, struct mdp5_ctl *ctl)
{
	struct drm_device *dev = mdp5_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct mdp5_interface intf = {
			.num	= intf_num,
			.type	= intf_type,
			.mode	= intf_mode,
	};

	if ((intf_type == INTF_DSI) &&
		(intf_mode == MDP5_INTF_DSI_MODE_COMMAND))
		encoder = mdp5_cmd_encoder_init(dev, &intf, ctl);
	else
		encoder = mdp5_encoder_init(dev, &intf, ctl);

	if (IS_ERR(encoder)) {
		dev_err(dev->dev, "failed to construct encoder\n");
		return encoder;
	}

	encoder->possible_crtcs = (1 << priv->num_crtcs) - 1;
	priv->encoders[priv->num_encoders++] = encoder;

	return encoder;
}

static int get_dsi_id_from_intf(const struct mdp5_cfg_hw *hw_cfg, int intf_num)
{
	const enum mdp5_intf_type *intfs = hw_cfg->intf.connect;
	const int intf_cnt = ARRAY_SIZE(hw_cfg->intf.connect);
	int id = 0, i;

	for (i = 0; i < intf_cnt; i++) {
		if (intfs[i] == INTF_DSI) {
			if (intf_num == i)
				return id;

			id++;
		}
	}

	return -EINVAL;
}

static int modeset_init_intf(struct mdp5_kms *mdp5_kms, int intf_num)
{
	struct drm_device *dev = mdp5_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	const struct mdp5_cfg_hw *hw_cfg =
					mdp5_cfg_get_hw_config(mdp5_kms->cfg);
	enum mdp5_intf_type intf_type = hw_cfg->intf.connect[intf_num];
	struct mdp5_ctl_manager *ctlm = mdp5_kms->ctlm;
	struct mdp5_ctl *ctl;
	struct drm_encoder *encoder;
	int ret = 0;

	switch (intf_type) {
	case INTF_DISABLED:
		break;
	case INTF_eDP:
		if (!priv->edp)
			break;

		ctl = mdp5_ctlm_request(ctlm, intf_num);
		if (!ctl) {
			ret = -EINVAL;
			break;
		}

		encoder = construct_encoder(mdp5_kms, INTF_eDP, intf_num,
					MDP5_INTF_MODE_NONE, ctl);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			break;
		}

		ret = msm_edp_modeset_init(priv->edp, dev, encoder);
		break;
	case INTF_HDMI:
		if (!priv->hdmi)
			break;

		ctl = mdp5_ctlm_request(ctlm, intf_num);
		if (!ctl) {
			ret = -EINVAL;
			break;
		}

		encoder = construct_encoder(mdp5_kms, INTF_HDMI, intf_num,
					MDP5_INTF_MODE_NONE, ctl);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			break;
		}

		ret = msm_hdmi_modeset_init(priv->hdmi, dev, encoder);
		break;
	case INTF_DSI:
	{
		int dsi_id = get_dsi_id_from_intf(hw_cfg, intf_num);

		if ((dsi_id >= ARRAY_SIZE(priv->dsi)) || (dsi_id < 0)) {
			dev_err(dev->dev, "failed to find dsi from intf %d\n",
				intf_num);
			ret = -EINVAL;
			break;
		}

		if (!priv->dsi[dsi_id])
			break;

		ctl = mdp5_ctlm_request(ctlm, intf_num);
		if (!ctl) {
			ret = -EINVAL;
			break;
		}

		encoder = construct_encoder(mdp5_kms, INTF_DSI, intf_num,
					    MDP5_INTF_DSI_MODE_VIDEO, ctl);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			break;
		}

		ret = msm_dsi_modeset_init(priv->dsi[dsi_id], dev, encoder);
		break;
	}
	default:
		dev_err(dev->dev, "unknown intf: %d\n", intf_type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int modeset_init(struct mdp5_kms *mdp5_kms)
{
	struct drm_device *dev = mdp5_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	const struct mdp5_cfg_hw *hw_cfg;
	int i, ret;

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	/* Construct planes equaling the number of hw pipes, and CRTCs
	 * for the N layer-mixers (LM).  The first N planes become primary
	 * planes for the CRTCs, with the remainder as overlay planes:
	 */
	for (i = 0; i < mdp5_kms->num_hwpipes; i++) {
		bool primary = i < mdp5_cfg->lm.count;
		struct drm_plane *plane;
		struct drm_crtc *crtc;

		plane = mdp5_plane_init(dev, primary);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			dev_err(dev->dev, "failed to construct plane %d (%d)\n", i, ret);
			goto fail;
		}
		priv->planes[priv->num_planes++] = plane;

		if (!primary)
			continue;

		crtc  = mdp5_crtc_init(dev, plane, i);
		if (IS_ERR(crtc)) {
			ret = PTR_ERR(crtc);
			dev_err(dev->dev, "failed to construct crtc %d (%d)\n", i, ret);
			goto fail;
		}
		priv->crtcs[priv->num_crtcs++] = crtc;
	}

	/* Construct encoders and modeset initialize connector devices
	 * for each external display interface.
	 */
	for (i = 0; i < ARRAY_SIZE(hw_cfg->intf.connect); i++) {
		ret = modeset_init_intf(mdp5_kms, i);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	return ret;
}

static void read_mdp_hw_revision(struct mdp5_kms *mdp5_kms,
				 u32 *major, u32 *minor)
{
	u32 version;

	mdp5_enable(mdp5_kms);
	version = mdp5_read(mdp5_kms, REG_MDP5_HW_VERSION);
	mdp5_disable(mdp5_kms);

	*major = FIELD(version, MDP5_HW_VERSION_MAJOR);
	*minor = FIELD(version, MDP5_HW_VERSION_MINOR);

	DBG("MDP5 version v%d.%d", *major, *minor);
}

static int get_clk(struct platform_device *pdev, struct clk **clkp,
		const char *name, bool mandatory)
{
	struct device *dev = &pdev->dev;
	struct clk *clk = devm_clk_get(dev, name);
	if (IS_ERR(clk) && mandatory) {
		dev_err(dev, "failed to get %s (%ld)\n", name, PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	if (IS_ERR(clk))
		DBG("skipping %s", name);
	else
		*clkp = clk;

	return 0;
}

static struct drm_encoder *get_encoder_from_crtc(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, dev)
		if (encoder->crtc == crtc)
			return encoder;

	return NULL;
}

static int mdp5_get_scanoutpos(struct drm_device *dev, unsigned int pipe,
			       unsigned int flags, int *vpos, int *hpos,
			       ktime_t *stime, ktime_t *etime,
			       const struct drm_display_mode *mode)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int line, vsw, vbp, vactive_start, vactive_end, vfp_end;
	int ret = 0;

	crtc = priv->crtcs[pipe];
	if (!crtc) {
		DRM_ERROR("Invalid crtc %d\n", pipe);
		return 0;
	}

	encoder = get_encoder_from_crtc(crtc);
	if (!encoder) {
		DRM_ERROR("no encoder found for crtc %d\n", pipe);
		return 0;
	}

	ret |= DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_ACCURATE;

	vsw = mode->crtc_vsync_end - mode->crtc_vsync_start;
	vbp = mode->crtc_vtotal - mode->crtc_vsync_end;

	/*
	 * the line counter is 1 at the start of the VSYNC pulse and VTOTAL at
	 * the end of VFP. Translate the porch values relative to the line
	 * counter positions.
	 */

	vactive_start = vsw + vbp + 1;

	vactive_end = vactive_start + mode->crtc_vdisplay;

	/* last scan line before VSYNC */
	vfp_end = mode->crtc_vtotal;

	if (stime)
		*stime = ktime_get();

	line = mdp5_encoder_get_linecount(encoder);

	if (line < vactive_start) {
		line -= vactive_start;
		ret |= DRM_SCANOUTPOS_IN_VBLANK;
	} else if (line > vactive_end) {
		line = line - vfp_end - vactive_start;
		ret |= DRM_SCANOUTPOS_IN_VBLANK;
	} else {
		line -= vactive_start;
	}

	*vpos = line;
	*hpos = 0;

	if (etime)
		*etime = ktime_get();

	return ret;
}

static int mdp5_get_vblank_timestamp(struct drm_device *dev, unsigned int pipe,
				     int *max_error,
				     struct timeval *vblank_time,
				     unsigned flags)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;

	if (pipe < 0 || pipe >= priv->num_crtcs) {
		DRM_ERROR("Invalid crtc %d\n", pipe);
		return -EINVAL;
	}

	crtc = priv->crtcs[pipe];
	if (!crtc) {
		DRM_ERROR("Invalid crtc %d\n", pipe);
		return -EINVAL;
	}

	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error,
						     vblank_time, flags,
						     &crtc->mode);
}

static u32 mdp5_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;

	if (pipe < 0 || pipe >= priv->num_crtcs)
		return 0;

	crtc = priv->crtcs[pipe];
	if (!crtc)
		return 0;

	encoder = get_encoder_from_crtc(crtc);
	if (!encoder)
		return 0;

	return mdp5_encoder_get_framecount(encoder);
}

struct msm_kms *mdp5_kms_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev;
	struct mdp5_kms *mdp5_kms;
	struct mdp5_cfg *config;
	struct msm_kms *kms;
	struct msm_gem_address_space *aspace;
	int irq, i, ret;

	/* priv->kms would have been populated by the MDP5 driver */
	kms = priv->kms;
	if (!kms)
		return NULL;

	mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));

	mdp_kms_init(&mdp5_kms->base, &kms_funcs);

	pdev = mdp5_kms->pdev;

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (irq < 0) {
		ret = irq;
		dev_err(&pdev->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	kms->irq = irq;

	config = mdp5_cfg_get_config(mdp5_kms->cfg);

	/* make sure things are off before attaching iommu (bootloader could
	 * have left things on, in which case we'll start getting faults if
	 * we don't disable):
	 */
	mdp5_enable(mdp5_kms);
	for (i = 0; i < MDP5_INTF_NUM_MAX; i++) {
		if (mdp5_cfg_intf_is_virtual(config->hw->intf.connect[i]) ||
		    !config->hw->intf.base[i])
			continue;
		mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(i), 0);

		mdp5_write(mdp5_kms, REG_MDP5_INTF_FRAME_LINE_COUNT_EN(i), 0x3);
	}
	mdp5_disable(mdp5_kms);
	mdelay(16);

	if (config->platform.iommu) {
		aspace = msm_gem_address_space_create(&pdev->dev,
				config->platform.iommu, "mdp5");
		if (IS_ERR(aspace)) {
			ret = PTR_ERR(aspace);
			goto fail;
		}

		mdp5_kms->aspace = aspace;

		ret = aspace->mmu->funcs->attach(aspace->mmu, iommu_ports,
				ARRAY_SIZE(iommu_ports));
		if (ret) {
			dev_err(&pdev->dev, "failed to attach iommu: %d\n",
				ret);
			goto fail;
		}
	} else {
		dev_info(&pdev->dev,
			 "no iommu, fallback to phys contig buffers for scanout\n");
		aspace = NULL;;
	}

	mdp5_kms->id = msm_register_address_space(dev, aspace);
	if (mdp5_kms->id < 0) {
		ret = mdp5_kms->id;
		dev_err(&pdev->dev, "failed to register mdp5 iommu: %d\n", ret);
		goto fail;
	}

	ret = modeset_init(mdp5_kms);
	if (ret) {
		dev_err(&pdev->dev, "modeset_init failed: %d\n", ret);
		goto fail;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 0xffff;
	dev->mode_config.max_height = 0xffff;

	dev->driver->get_vblank_timestamp = mdp5_get_vblank_timestamp;
	dev->driver->get_scanout_position = mdp5_get_scanoutpos;
	dev->driver->get_vblank_counter = mdp5_get_vblank_counter;
	dev->max_vblank_count = 0xffffffff;
	dev->vblank_disable_immediate = true;

	return kms;
fail:
	if (kms)
		mdp5_kms_destroy(kms);
	return ERR_PTR(ret);
}

static void mdp5_destroy(struct platform_device *pdev)
{
	struct mdp5_kms *mdp5_kms = platform_get_drvdata(pdev);

	if (mdp5_kms->ctlm)
		mdp5_ctlm_destroy(mdp5_kms->ctlm);
	if (mdp5_kms->smp)
		mdp5_smp_destroy(mdp5_kms->smp);
	if (mdp5_kms->cfg)
		mdp5_cfg_destroy(mdp5_kms->cfg);

	if (mdp5_kms->rpm_enabled)
		pm_runtime_disable(&pdev->dev);

	kfree(mdp5_kms->state);
}

static int construct_pipes(struct mdp5_kms *mdp5_kms, int cnt,
		const enum mdp5_pipe *pipes, const uint32_t *offsets,
		uint32_t caps)
{
	struct drm_device *dev = mdp5_kms->dev;
	int i, ret;

	for (i = 0; i < cnt; i++) {
		struct mdp5_hw_pipe *hwpipe;

		hwpipe = mdp5_pipe_init(pipes[i], offsets[i], caps);
		if (IS_ERR(hwpipe)) {
			ret = PTR_ERR(hwpipe);
			dev_err(dev->dev, "failed to construct pipe for %s (%d)\n",
					pipe2name(pipes[i]), ret);
			return ret;
		}
		hwpipe->idx = mdp5_kms->num_hwpipes;
		mdp5_kms->hwpipes[mdp5_kms->num_hwpipes++] = hwpipe;
	}

	return 0;
}

static int hwpipe_init(struct mdp5_kms *mdp5_kms)
{
	static const enum mdp5_pipe rgb_planes[] = {
			SSPP_RGB0, SSPP_RGB1, SSPP_RGB2, SSPP_RGB3,
	};
	static const enum mdp5_pipe vig_planes[] = {
			SSPP_VIG0, SSPP_VIG1, SSPP_VIG2, SSPP_VIG3,
	};
	static const enum mdp5_pipe dma_planes[] = {
			SSPP_DMA0, SSPP_DMA1,
	};
	const struct mdp5_cfg_hw *hw_cfg;
	int ret;

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	/* Construct RGB pipes: */
	ret = construct_pipes(mdp5_kms, hw_cfg->pipe_rgb.count, rgb_planes,
			hw_cfg->pipe_rgb.base, hw_cfg->pipe_rgb.caps);
	if (ret)
		return ret;

	/* Construct video (VIG) pipes: */
	ret = construct_pipes(mdp5_kms, hw_cfg->pipe_vig.count, vig_planes,
			hw_cfg->pipe_vig.base, hw_cfg->pipe_vig.caps);
	if (ret)
		return ret;

	/* Construct DMA pipes: */
	ret = construct_pipes(mdp5_kms, hw_cfg->pipe_dma.count, dma_planes,
			hw_cfg->pipe_dma.base, hw_cfg->pipe_dma.caps);
	if (ret)
		return ret;

	return 0;
}

static int mdp5_init(struct platform_device *pdev, struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct mdp5_kms *mdp5_kms;
	struct mdp5_cfg *config;
	u32 major, minor;
	int ret;

	mdp5_kms = devm_kzalloc(&pdev->dev, sizeof(*mdp5_kms), GFP_KERNEL);
	if (!mdp5_kms) {
		ret = -ENOMEM;
		goto fail;
	}

	platform_set_drvdata(pdev, mdp5_kms);

	spin_lock_init(&mdp5_kms->resource_lock);

	mdp5_kms->dev = dev;
	mdp5_kms->pdev = pdev;

	drm_modeset_lock_init(&mdp5_kms->state_lock);
	mdp5_kms->state = kzalloc(sizeof(*mdp5_kms->state), GFP_KERNEL);
	if (!mdp5_kms->state) {
		ret = -ENOMEM;
		goto fail;
	}

	mdp5_kms->mmio = msm_ioremap(pdev, "mdp_phys", "MDP5");
	if (IS_ERR(mdp5_kms->mmio)) {
		ret = PTR_ERR(mdp5_kms->mmio);
		goto fail;
	}

	/* mandatory clocks: */
	ret = get_clk(pdev, &mdp5_kms->axi_clk, "bus_clk", true);
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->ahb_clk, "iface_clk", true);
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->core_clk, "core_clk", true);
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->vsync_clk, "vsync_clk", true);
	if (ret)
		goto fail;

	/* optional clocks: */
	get_clk(pdev, &mdp5_kms->lut_clk, "lut_clk", false);

	/* we need to set a default rate before enabling.  Set a safe
	 * rate first, then figure out hw revision, and then set a
	 * more optimal rate:
	 */
	clk_set_rate(mdp5_kms->core_clk, 200000000);

	pm_runtime_enable(&pdev->dev);
	mdp5_kms->rpm_enabled = true;

	read_mdp_hw_revision(mdp5_kms, &major, &minor);

	mdp5_kms->cfg = mdp5_cfg_init(mdp5_kms, major, minor);
	if (IS_ERR(mdp5_kms->cfg)) {
		ret = PTR_ERR(mdp5_kms->cfg);
		mdp5_kms->cfg = NULL;
		goto fail;
	}

	config = mdp5_cfg_get_config(mdp5_kms->cfg);
	mdp5_kms->caps = config->hw->mdp.caps;

	/* TODO: compute core clock rate at runtime */
	clk_set_rate(mdp5_kms->core_clk, config->hw->max_clk);

	/*
	 * Some chipsets have a Shared Memory Pool (SMP), while others
	 * have dedicated latency buffering per source pipe instead;
	 * this section initializes the SMP:
	 */
	if (mdp5_kms->caps & MDP_CAP_SMP) {
		mdp5_kms->smp = mdp5_smp_init(mdp5_kms, &config->hw->smp);
		if (IS_ERR(mdp5_kms->smp)) {
			ret = PTR_ERR(mdp5_kms->smp);
			mdp5_kms->smp = NULL;
			goto fail;
		}
	}

	mdp5_kms->ctlm = mdp5_ctlm_init(dev, mdp5_kms->mmio, mdp5_kms->cfg);
	if (IS_ERR(mdp5_kms->ctlm)) {
		ret = PTR_ERR(mdp5_kms->ctlm);
		mdp5_kms->ctlm = NULL;
		goto fail;
	}

	ret = hwpipe_init(mdp5_kms);
	if (ret)
		goto fail;

	/* set uninit-ed kms */
	priv->kms = &mdp5_kms->base.base;

	return 0;
fail:
	mdp5_destroy(pdev);
	return ret;
}

static int mdp5_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *ddev = dev_get_drvdata(master);
	struct platform_device *pdev = to_platform_device(dev);

	DBG("");

	return mdp5_init(pdev, ddev);
}

static void mdp5_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

	mdp5_destroy(pdev);
}

static const struct component_ops mdp5_ops = {
	.bind   = mdp5_bind,
	.unbind = mdp5_unbind,
};

static int mdp5_dev_probe(struct platform_device *pdev)
{
	DBG("");
	return component_add(&pdev->dev, &mdp5_ops);
}

static int mdp5_dev_remove(struct platform_device *pdev)
{
	DBG("");
	component_del(&pdev->dev, &mdp5_ops);
	return 0;
}

static const struct of_device_id mdp5_dt_match[] = {
	{ .compatible = "qcom,mdp5", },
	/* to support downstream DT files */
	{ .compatible = "qcom,mdss_mdp", },
	{}
};
MODULE_DEVICE_TABLE(of, mdp5_dt_match);

static struct platform_driver mdp5_driver = {
	.probe = mdp5_dev_probe,
	.remove = mdp5_dev_remove,
	.driver = {
		.name = "msm_mdp",
		.of_match_table = mdp5_dt_match,
	},
};

void __init msm_mdp_register(void)
{
	DBG("");
	platform_driver_register(&mdp5_driver);
}

void __exit msm_mdp_unregister(void)
{
	DBG("");
	platform_driver_unregister(&mdp5_driver);
}
