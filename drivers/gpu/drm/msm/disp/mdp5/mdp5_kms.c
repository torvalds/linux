// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <linux/interconnect.h>
#include <linux/of_irq.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_vblank.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "mdp5_kms.h"

static int mdp5_hw_init(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct device *dev = &mdp5_kms->pdev->dev;
	unsigned long flags;

	pm_runtime_get_sync(dev);

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

	pm_runtime_put_sync(dev);

	return 0;
}

/* Global/shared object state funcs */

/*
 * This is a helper that returns the private state currently in operation.
 * Note that this would return the "old_state" if called in the atomic check
 * path, and the "new_state" after the atomic swap has been done.
 */
struct mdp5_global_state *
mdp5_get_existing_global_state(struct mdp5_kms *mdp5_kms)
{
	return to_mdp5_global_state(mdp5_kms->glob_state.state);
}

/*
 * This acquires the modeset lock set aside for global state, creates
 * a new duplicated private object state.
 */
struct mdp5_global_state *mdp5_get_global_state(struct drm_atomic_state *s)
{
	struct msm_drm_private *priv = s->dev->dev_private;
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(priv->kms));
	struct drm_private_state *priv_state;
	int ret;

	ret = drm_modeset_lock(&mdp5_kms->glob_state_lock, s->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	priv_state = drm_atomic_get_private_obj_state(s, &mdp5_kms->glob_state);
	if (IS_ERR(priv_state))
		return ERR_CAST(priv_state);

	return to_mdp5_global_state(priv_state);
}

static struct drm_private_state *
mdp5_global_duplicate_state(struct drm_private_obj *obj)
{
	struct mdp5_global_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void mdp5_global_destroy_state(struct drm_private_obj *obj,
				      struct drm_private_state *state)
{
	struct mdp5_global_state *mdp5_state = to_mdp5_global_state(state);

	kfree(mdp5_state);
}

static const struct drm_private_state_funcs mdp5_global_state_funcs = {
	.atomic_duplicate_state = mdp5_global_duplicate_state,
	.atomic_destroy_state = mdp5_global_destroy_state,
};

static int mdp5_global_obj_init(struct mdp5_kms *mdp5_kms)
{
	struct mdp5_global_state *state;

	drm_modeset_lock_init(&mdp5_kms->glob_state_lock);

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->mdp5_kms = mdp5_kms;

	drm_atomic_private_obj_init(mdp5_kms->dev, &mdp5_kms->glob_state,
				    &state->base,
				    &mdp5_global_state_funcs);
	return 0;
}

static void mdp5_enable_commit(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	pm_runtime_get_sync(&mdp5_kms->pdev->dev);
}

static void mdp5_disable_commit(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	pm_runtime_put_sync(&mdp5_kms->pdev->dev);
}

static void mdp5_prepare_commit(struct msm_kms *kms, struct drm_atomic_state *state)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct mdp5_global_state *global_state;

	global_state = mdp5_get_existing_global_state(mdp5_kms);

	if (mdp5_kms->smp)
		mdp5_smp_prepare_commit(mdp5_kms->smp, &global_state->smp);
}

static void mdp5_flush_commit(struct msm_kms *kms, unsigned crtc_mask)
{
	/* TODO */
}

static void mdp5_wait_flush(struct msm_kms *kms, unsigned crtc_mask)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct drm_crtc *crtc;

	for_each_crtc_mask(mdp5_kms->dev, crtc, crtc_mask)
		mdp5_crtc_wait_for_commit_done(crtc);
}

static void mdp5_complete_commit(struct msm_kms *kms, unsigned crtc_mask)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct mdp5_global_state *global_state;

	global_state = mdp5_get_existing_global_state(mdp5_kms);

	if (mdp5_kms->smp)
		mdp5_smp_complete_commit(mdp5_kms->smp, &global_state->smp);
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
		return mdp5_vid_encoder_set_split_display(encoder,
							  slave_encoder);
}

static void mdp5_kms_destroy(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct msm_gem_address_space *aspace = kms->aspace;
	int i;

	for (i = 0; i < mdp5_kms->num_hwmixers; i++)
		mdp5_mixer_destroy(mdp5_kms->hwmixers[i]);

	for (i = 0; i < mdp5_kms->num_hwpipes; i++)
		mdp5_pipe_destroy(mdp5_kms->hwpipes[i]);

	if (aspace) {
		aspace->mmu->funcs->detach(aspace->mmu);
		msm_gem_address_space_put(aspace);
	}

	mdp_kms_destroy(&mdp5_kms->base);
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
	drm_debugfs_create_files(mdp5_debugfs_list,
				 ARRAY_SIZE(mdp5_debugfs_list),
				 minor->debugfs_root, minor);

	return 0;
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
		.flush_commit    = mdp5_flush_commit,
		.enable_commit   = mdp5_enable_commit,
		.disable_commit  = mdp5_disable_commit,
		.prepare_commit  = mdp5_prepare_commit,
		.wait_flush      = mdp5_wait_flush,
		.complete_commit = mdp5_complete_commit,
		.get_format      = mdp_get_format,
		.round_pixclk    = mdp5_round_pixclk,
		.set_split_display = mdp5_set_split_display,
		.destroy         = mdp5_kms_destroy,
#ifdef CONFIG_DEBUG_FS
		.debugfs_init    = mdp5_kms_debugfs_init,
#endif
	},
	.set_irqmask         = mdp5_set_irqmask,
};

static int mdp5_disable(struct mdp5_kms *mdp5_kms)
{
	DBG("");

	mdp5_kms->enable_count--;
	WARN_ON(mdp5_kms->enable_count < 0);

	clk_disable_unprepare(mdp5_kms->tbu_rt_clk);
	clk_disable_unprepare(mdp5_kms->tbu_clk);
	clk_disable_unprepare(mdp5_kms->ahb_clk);
	clk_disable_unprepare(mdp5_kms->axi_clk);
	clk_disable_unprepare(mdp5_kms->core_clk);
	clk_disable_unprepare(mdp5_kms->lut_clk);

	return 0;
}

static int mdp5_enable(struct mdp5_kms *mdp5_kms)
{
	DBG("");

	mdp5_kms->enable_count++;

	clk_prepare_enable(mdp5_kms->ahb_clk);
	clk_prepare_enable(mdp5_kms->axi_clk);
	clk_prepare_enable(mdp5_kms->core_clk);
	clk_prepare_enable(mdp5_kms->lut_clk);
	clk_prepare_enable(mdp5_kms->tbu_clk);
	clk_prepare_enable(mdp5_kms->tbu_rt_clk);

	return 0;
}

static struct drm_encoder *construct_encoder(struct mdp5_kms *mdp5_kms,
					     struct mdp5_interface *intf,
					     struct mdp5_ctl *ctl)
{
	struct drm_device *dev = mdp5_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder;

	encoder = mdp5_encoder_init(dev, intf, ctl);
	if (IS_ERR(encoder)) {
		DRM_DEV_ERROR(dev->dev, "failed to construct encoder\n");
		return encoder;
	}

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

static int modeset_init_intf(struct mdp5_kms *mdp5_kms,
			     struct mdp5_interface *intf)
{
	struct drm_device *dev = mdp5_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct mdp5_ctl_manager *ctlm = mdp5_kms->ctlm;
	struct mdp5_ctl *ctl;
	struct drm_encoder *encoder;
	int ret = 0;

	switch (intf->type) {
	case INTF_eDP:
		DRM_DEV_INFO(dev->dev, "Skipping eDP interface %d\n", intf->num);
		break;
	case INTF_HDMI:
		if (!priv->hdmi)
			break;

		ctl = mdp5_ctlm_request(ctlm, intf->num);
		if (!ctl) {
			ret = -EINVAL;
			break;
		}

		encoder = construct_encoder(mdp5_kms, intf, ctl);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			break;
		}

		ret = msm_hdmi_modeset_init(priv->hdmi, dev, encoder);
		break;
	case INTF_DSI:
	{
		const struct mdp5_cfg_hw *hw_cfg =
					mdp5_cfg_get_hw_config(mdp5_kms->cfg);
		int dsi_id = get_dsi_id_from_intf(hw_cfg, intf->num);

		if ((dsi_id >= ARRAY_SIZE(priv->dsi)) || (dsi_id < 0)) {
			DRM_DEV_ERROR(dev->dev, "failed to find dsi from intf %d\n",
				intf->num);
			ret = -EINVAL;
			break;
		}

		if (!priv->dsi[dsi_id])
			break;

		ctl = mdp5_ctlm_request(ctlm, intf->num);
		if (!ctl) {
			ret = -EINVAL;
			break;
		}

		encoder = construct_encoder(mdp5_kms, intf, ctl);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			break;
		}

		ret = msm_dsi_modeset_init(priv->dsi[dsi_id], dev, encoder);
		if (!ret)
			mdp5_encoder_set_intf_mode(encoder, msm_dsi_is_cmd_mode(priv->dsi[dsi_id]));

		break;
	}
	default:
		DRM_DEV_ERROR(dev->dev, "unknown intf: %d\n", intf->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int modeset_init(struct mdp5_kms *mdp5_kms)
{
	struct drm_device *dev = mdp5_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	unsigned int num_crtcs;
	int i, ret, pi = 0, ci = 0;
	struct drm_plane *primary[MAX_BASES] = { NULL };
	struct drm_plane *cursor[MAX_BASES] = { NULL };

	/*
	 * Construct encoders and modeset initialize connector devices
	 * for each external display interface.
	 */
	for (i = 0; i < mdp5_kms->num_intfs; i++) {
		ret = modeset_init_intf(mdp5_kms, mdp5_kms->intfs[i]);
		if (ret)
			goto fail;
	}

	/*
	 * We should ideally have less number of encoders (set up by parsing
	 * the MDP5 interfaces) than the number of layer mixers present in HW,
	 * but let's be safe here anyway
	 */
	num_crtcs = min(priv->num_encoders, mdp5_kms->num_hwmixers);

	/*
	 * Construct planes equaling the number of hw pipes, and CRTCs for the
	 * N encoders set up by the driver. The first N planes become primary
	 * planes for the CRTCs, with the remainder as overlay planes:
	 */
	for (i = 0; i < mdp5_kms->num_hwpipes; i++) {
		struct mdp5_hw_pipe *hwpipe = mdp5_kms->hwpipes[i];
		struct drm_plane *plane;
		enum drm_plane_type type;

		if (i < num_crtcs)
			type = DRM_PLANE_TYPE_PRIMARY;
		else if (hwpipe->caps & MDP_PIPE_CAP_CURSOR)
			type = DRM_PLANE_TYPE_CURSOR;
		else
			type = DRM_PLANE_TYPE_OVERLAY;

		plane = mdp5_plane_init(dev, type);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			DRM_DEV_ERROR(dev->dev, "failed to construct plane %d (%d)\n", i, ret);
			goto fail;
		}
		priv->planes[priv->num_planes++] = plane;

		if (type == DRM_PLANE_TYPE_PRIMARY)
			primary[pi++] = plane;
		if (type == DRM_PLANE_TYPE_CURSOR)
			cursor[ci++] = plane;
	}

	for (i = 0; i < num_crtcs; i++) {
		struct drm_crtc *crtc;

		crtc  = mdp5_crtc_init(dev, primary[i], cursor[i], i);
		if (IS_ERR(crtc)) {
			ret = PTR_ERR(crtc);
			DRM_DEV_ERROR(dev->dev, "failed to construct crtc %d (%d)\n", i, ret);
			goto fail;
		}
		priv->crtcs[priv->num_crtcs++] = crtc;
	}

	/*
	 * Now that we know the number of crtcs we've created, set the possible
	 * crtcs for the encoders
	 */
	for (i = 0; i < priv->num_encoders; i++) {
		struct drm_encoder *encoder = priv->encoders[i];

		encoder->possible_crtcs = (1 << priv->num_crtcs) - 1;
	}

	return 0;

fail:
	return ret;
}

static void read_mdp_hw_revision(struct mdp5_kms *mdp5_kms,
				 u32 *major, u32 *minor)
{
	struct device *dev = &mdp5_kms->pdev->dev;
	u32 version;

	pm_runtime_get_sync(dev);
	version = mdp5_read(mdp5_kms, REG_MDP5_HW_VERSION);
	pm_runtime_put_sync(dev);

	*major = FIELD(version, MDP5_HW_VERSION_MAJOR);
	*minor = FIELD(version, MDP5_HW_VERSION_MINOR);

	DRM_DEV_INFO(dev, "MDP5 version v%d.%d", *major, *minor);
}

static int get_clk(struct platform_device *pdev, struct clk **clkp,
		const char *name, bool mandatory)
{
	struct device *dev = &pdev->dev;
	struct clk *clk = msm_clk_get(pdev, name);
	if (IS_ERR(clk) && mandatory) {
		DRM_DEV_ERROR(dev, "failed to get %s (%ld)\n", name, PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	if (IS_ERR(clk))
		DBG("skipping %s", name);
	else
		*clkp = clk;

	return 0;
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
	struct device *iommu_dev;

	/* priv->kms would have been populated by the MDP5 driver */
	kms = priv->kms;
	if (!kms)
		return NULL;

	mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	pdev = mdp5_kms->pdev;

	ret = mdp_kms_init(&mdp5_kms->base, &kms_funcs);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to init kms\n");
		goto fail;
	}

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (irq < 0) {
		ret = irq;
		DRM_DEV_ERROR(&pdev->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	kms->irq = irq;

	config = mdp5_cfg_get_config(mdp5_kms->cfg);

	/* make sure things are off before attaching iommu (bootloader could
	 * have left things on, in which case we'll start getting faults if
	 * we don't disable):
	 */
	pm_runtime_get_sync(&pdev->dev);
	for (i = 0; i < MDP5_INTF_NUM_MAX; i++) {
		if (mdp5_cfg_intf_is_virtual(config->hw->intf.connect[i]) ||
		    !config->hw->intf.base[i])
			continue;
		mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(i), 0);

		mdp5_write(mdp5_kms, REG_MDP5_INTF_FRAME_LINE_COUNT_EN(i), 0x3);
	}
	mdelay(16);

	if (config->platform.iommu) {
		struct msm_mmu *mmu;

		iommu_dev = &pdev->dev;
		if (!dev_iommu_fwspec_get(iommu_dev))
			iommu_dev = iommu_dev->parent;

		mmu = msm_iommu_new(iommu_dev, config->platform.iommu);

		aspace = msm_gem_address_space_create(mmu, "mdp5",
			0x1000, 0x100000000 - 0x1000);

		if (IS_ERR(aspace)) {
			if (!IS_ERR(mmu))
				mmu->funcs->destroy(mmu);
			ret = PTR_ERR(aspace);
			goto fail;
		}

		kms->aspace = aspace;
	} else {
		DRM_DEV_INFO(&pdev->dev,
			 "no iommu, fallback to phys contig buffers for scanout\n");
		aspace = NULL;
	}

	pm_runtime_put_sync(&pdev->dev);

	ret = modeset_init(mdp5_kms);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "modeset_init failed: %d\n", ret);
		goto fail;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 0xffff;
	dev->mode_config.max_height = 0xffff;

	dev->max_vblank_count = 0; /* max_vblank_count is set on each CRTC */
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
	int i;

	if (mdp5_kms->ctlm)
		mdp5_ctlm_destroy(mdp5_kms->ctlm);
	if (mdp5_kms->smp)
		mdp5_smp_destroy(mdp5_kms->smp);
	if (mdp5_kms->cfg)
		mdp5_cfg_destroy(mdp5_kms->cfg);

	for (i = 0; i < mdp5_kms->num_intfs; i++)
		kfree(mdp5_kms->intfs[i]);

	if (mdp5_kms->rpm_enabled)
		pm_runtime_disable(&pdev->dev);

	drm_atomic_private_obj_fini(&mdp5_kms->glob_state);
	drm_modeset_lock_fini(&mdp5_kms->glob_state_lock);
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
			DRM_DEV_ERROR(dev->dev, "failed to construct pipe for %s (%d)\n",
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
	static const enum mdp5_pipe cursor_planes[] = {
			SSPP_CURSOR0, SSPP_CURSOR1,
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

	/* Construct cursor pipes: */
	ret = construct_pipes(mdp5_kms, hw_cfg->pipe_cursor.count,
			cursor_planes, hw_cfg->pipe_cursor.base,
			hw_cfg->pipe_cursor.caps);
	if (ret)
		return ret;

	return 0;
}

static int hwmixer_init(struct mdp5_kms *mdp5_kms)
{
	struct drm_device *dev = mdp5_kms->dev;
	const struct mdp5_cfg_hw *hw_cfg;
	int i, ret;

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	for (i = 0; i < hw_cfg->lm.count; i++) {
		struct mdp5_hw_mixer *mixer;

		mixer = mdp5_mixer_init(&hw_cfg->lm.instances[i]);
		if (IS_ERR(mixer)) {
			ret = PTR_ERR(mixer);
			DRM_DEV_ERROR(dev->dev, "failed to construct LM%d (%d)\n",
				i, ret);
			return ret;
		}

		mixer->idx = mdp5_kms->num_hwmixers;
		mdp5_kms->hwmixers[mdp5_kms->num_hwmixers++] = mixer;
	}

	return 0;
}

static int interface_init(struct mdp5_kms *mdp5_kms)
{
	struct drm_device *dev = mdp5_kms->dev;
	const struct mdp5_cfg_hw *hw_cfg;
	const enum mdp5_intf_type *intf_types;
	int i;

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);
	intf_types = hw_cfg->intf.connect;

	for (i = 0; i < ARRAY_SIZE(hw_cfg->intf.connect); i++) {
		struct mdp5_interface *intf;

		if (intf_types[i] == INTF_DISABLED)
			continue;

		intf = kzalloc(sizeof(*intf), GFP_KERNEL);
		if (!intf) {
			DRM_DEV_ERROR(dev->dev, "failed to construct INTF%d\n", i);
			return -ENOMEM;
		}

		intf->num = i;
		intf->type = intf_types[i];
		intf->mode = MDP5_INTF_MODE_NONE;
		intf->idx = mdp5_kms->num_intfs;
		mdp5_kms->intfs[mdp5_kms->num_intfs++] = intf;
	}

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

	ret = mdp5_global_obj_init(mdp5_kms);
	if (ret)
		goto fail;

	mdp5_kms->mmio = msm_ioremap(pdev, "mdp_phys");
	if (IS_ERR(mdp5_kms->mmio)) {
		ret = PTR_ERR(mdp5_kms->mmio);
		goto fail;
	}

	/* mandatory clocks: */
	ret = get_clk(pdev, &mdp5_kms->axi_clk, "bus", true);
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->ahb_clk, "iface", true);
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->core_clk, "core", true);
	if (ret)
		goto fail;
	ret = get_clk(pdev, &mdp5_kms->vsync_clk, "vsync", true);
	if (ret)
		goto fail;

	/* optional clocks: */
	get_clk(pdev, &mdp5_kms->lut_clk, "lut", false);
	get_clk(pdev, &mdp5_kms->tbu_clk, "tbu", false);
	get_clk(pdev, &mdp5_kms->tbu_rt_clk, "tbu_rt", false);

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

	ret = hwmixer_init(mdp5_kms);
	if (ret)
		goto fail;

	ret = interface_init(mdp5_kms);
	if (ret)
		goto fail;

	/* set uninit-ed kms */
	priv->kms = &mdp5_kms->base.base;

	return 0;
fail:
	if (mdp5_kms)
		mdp5_destroy(pdev);
	return ret;
}

static int mdp5_bind(struct device *dev, struct device *master, void *data)
{
	struct msm_drm_private *priv = dev_get_drvdata(master);
	struct drm_device *ddev = priv->dev;
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

static int mdp5_setup_interconnect(struct platform_device *pdev)
{
	struct icc_path *path0 = of_icc_get(&pdev->dev, "mdp0-mem");
	struct icc_path *path1 = of_icc_get(&pdev->dev, "mdp1-mem");
	struct icc_path *path_rot = of_icc_get(&pdev->dev, "rotator-mem");

	if (IS_ERR(path0))
		return PTR_ERR(path0);

	if (!path0) {
		/* no interconnect support is not necessarily a fatal
		 * condition, the platform may simply not have an
		 * interconnect driver yet.  But warn about it in case
		 * bootloader didn't setup bus clocks high enough for
		 * scanout.
		 */
		dev_warn(&pdev->dev, "No interconnect support may cause display underflows!\n");
		return 0;
	}

	icc_set_bw(path0, 0, MBps_to_icc(6400));

	if (!IS_ERR_OR_NULL(path1))
		icc_set_bw(path1, 0, MBps_to_icc(6400));
	if (!IS_ERR_OR_NULL(path_rot))
		icc_set_bw(path_rot, 0, MBps_to_icc(6400));

	return 0;
}

static int mdp5_dev_probe(struct platform_device *pdev)
{
	int ret;

	DBG("");

	ret = mdp5_setup_interconnect(pdev);
	if (ret)
		return ret;

	return component_add(&pdev->dev, &mdp5_ops);
}

static int mdp5_dev_remove(struct platform_device *pdev)
{
	DBG("");
	component_del(&pdev->dev, &mdp5_ops);
	return 0;
}

static __maybe_unused int mdp5_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdp5_kms *mdp5_kms = platform_get_drvdata(pdev);

	DBG("");

	return mdp5_disable(mdp5_kms);
}

static __maybe_unused int mdp5_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdp5_kms *mdp5_kms = platform_get_drvdata(pdev);

	DBG("");

	return mdp5_enable(mdp5_kms);
}

static const struct dev_pm_ops mdp5_pm_ops = {
	SET_RUNTIME_PM_OPS(mdp5_runtime_suspend, mdp5_runtime_resume, NULL)
};

const struct of_device_id mdp5_dt_match[] = {
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
		.pm = &mdp5_pm_ops,
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
