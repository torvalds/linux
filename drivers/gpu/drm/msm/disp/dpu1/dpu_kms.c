// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Author: Rob Clark <robdclark@gmail.com>
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/debugfs.h>
#include <linux/dma-buf.h>
#include <linux/of_irq.h>
#include <linux/pm_opp.h>

#include <drm/drm_crtc.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_vblank.h>
#include <drm/drm_writeback.h>

#include "msm_drv.h"
#include "msm_mmu.h"
#include "msm_mdss.h"
#include "msm_gem.h"
#include "disp/msm_disp_snapshot.h"

#include "dpu_core_irq.h"
#include "dpu_crtc.h"
#include "dpu_encoder.h"
#include "dpu_formats.h"
#include "dpu_hw_vbif.h"
#include "dpu_kms.h"
#include "dpu_plane.h"
#include "dpu_vbif.h"
#include "dpu_writeback.h"

#define CREATE_TRACE_POINTS
#include "dpu_trace.h"

/*
 * To enable overall DRM driver logging
 * # echo 0x2 > /sys/module/drm/parameters/debug
 *
 * To enable DRM driver h/w logging
 * # echo <mask> > /sys/kernel/debug/dri/0/debug/hw_log_mask
 *
 * See dpu_hw_mdss.h for h/w logging mask definitions (search for DPU_DBG_MASK_)
 */
#define DPU_DEBUGFS_DIR "msm_dpu"
#define DPU_DEBUGFS_HWMASKNAME "hw_log_mask"

static int dpu_kms_hw_init(struct msm_kms *kms);
static void _dpu_kms_mmu_destroy(struct dpu_kms *dpu_kms);

#ifdef CONFIG_DEBUG_FS
static int _dpu_danger_signal_status(struct seq_file *s,
		bool danger_status)
{
	struct dpu_danger_safe_status status;
	struct dpu_kms *kms = s->private;
	int i;

	if (!kms->hw_mdp) {
		DPU_ERROR("invalid arg(s)\n");
		return 0;
	}

	memset(&status, 0, sizeof(struct dpu_danger_safe_status));

	pm_runtime_get_sync(&kms->pdev->dev);
	if (danger_status) {
		seq_puts(s, "\nDanger signal status:\n");
		if (kms->hw_mdp->ops.get_danger_status)
			kms->hw_mdp->ops.get_danger_status(kms->hw_mdp,
					&status);
	} else {
		seq_puts(s, "\nSafe signal status:\n");
		if (kms->hw_mdp->ops.get_safe_status)
			kms->hw_mdp->ops.get_safe_status(kms->hw_mdp,
					&status);
	}
	pm_runtime_put_sync(&kms->pdev->dev);

	seq_printf(s, "MDP     :  0x%x\n", status.mdp);

	for (i = SSPP_VIG0; i < SSPP_MAX; i++)
		seq_printf(s, "SSPP%d   :  0x%x  \n", i - SSPP_VIG0,
				status.sspp[i]);
	seq_puts(s, "\n");

	return 0;
}

static int dpu_debugfs_danger_stats_show(struct seq_file *s, void *v)
{
	return _dpu_danger_signal_status(s, true);
}
DEFINE_SHOW_ATTRIBUTE(dpu_debugfs_danger_stats);

static int dpu_debugfs_safe_stats_show(struct seq_file *s, void *v)
{
	return _dpu_danger_signal_status(s, false);
}
DEFINE_SHOW_ATTRIBUTE(dpu_debugfs_safe_stats);

static ssize_t _dpu_plane_danger_read(struct file *file,
			char __user *buff, size_t count, loff_t *ppos)
{
	struct dpu_kms *kms = file->private_data;
	int len;
	char buf[40];

	len = scnprintf(buf, sizeof(buf), "%d\n", !kms->has_danger_ctrl);

	return simple_read_from_buffer(buff, count, ppos, buf, len);
}

static void _dpu_plane_set_danger_state(struct dpu_kms *kms, bool enable)
{
	struct drm_plane *plane;

	drm_for_each_plane(plane, kms->dev) {
		if (plane->fb && plane->state) {
			dpu_plane_danger_signal_ctrl(plane, enable);
			DPU_DEBUG("plane:%d img:%dx%d ",
				plane->base.id, plane->fb->width,
				plane->fb->height);
			DPU_DEBUG("src[%d,%d,%d,%d] dst[%d,%d,%d,%d]\n",
				plane->state->src_x >> 16,
				plane->state->src_y >> 16,
				plane->state->src_w >> 16,
				plane->state->src_h >> 16,
				plane->state->crtc_x, plane->state->crtc_y,
				plane->state->crtc_w, plane->state->crtc_h);
		} else {
			DPU_DEBUG("Inactive plane:%d\n", plane->base.id);
		}
	}
}

static ssize_t _dpu_plane_danger_write(struct file *file,
		    const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct dpu_kms *kms = file->private_data;
	int disable_panic;
	int ret;

	ret = kstrtouint_from_user(user_buf, count, 0, &disable_panic);
	if (ret)
		return ret;

	if (disable_panic) {
		/* Disable panic signal for all active pipes */
		DPU_DEBUG("Disabling danger:\n");
		_dpu_plane_set_danger_state(kms, false);
		kms->has_danger_ctrl = false;
	} else {
		/* Enable panic signal for all active pipes */
		DPU_DEBUG("Enabling danger:\n");
		kms->has_danger_ctrl = true;
		_dpu_plane_set_danger_state(kms, true);
	}

	return count;
}

static const struct file_operations dpu_plane_danger_enable = {
	.open = simple_open,
	.read = _dpu_plane_danger_read,
	.write = _dpu_plane_danger_write,
};

static void dpu_debugfs_danger_init(struct dpu_kms *dpu_kms,
		struct dentry *parent)
{
	struct dentry *entry = debugfs_create_dir("danger", parent);

	debugfs_create_file("danger_status", 0600, entry,
			dpu_kms, &dpu_debugfs_danger_stats_fops);
	debugfs_create_file("safe_status", 0600, entry,
			dpu_kms, &dpu_debugfs_safe_stats_fops);
	debugfs_create_file("disable_danger", 0600, entry,
			dpu_kms, &dpu_plane_danger_enable);

}

/*
 * Companion structure for dpu_debugfs_create_regset32.
 */
struct dpu_debugfs_regset32 {
	uint32_t offset;
	uint32_t blk_len;
	struct dpu_kms *dpu_kms;
};

static int dpu_regset32_show(struct seq_file *s, void *data)
{
	struct dpu_debugfs_regset32 *regset = s->private;
	struct dpu_kms *dpu_kms = regset->dpu_kms;
	void __iomem *base;
	uint32_t i, addr;

	if (!dpu_kms->mmio)
		return 0;

	base = dpu_kms->mmio + regset->offset;

	/* insert padding spaces, if needed */
	if (regset->offset & 0xF) {
		seq_printf(s, "[%x]", regset->offset & ~0xF);
		for (i = 0; i < (regset->offset & 0xF); i += 4)
			seq_puts(s, "         ");
	}

	pm_runtime_get_sync(&dpu_kms->pdev->dev);

	/* main register output */
	for (i = 0; i < regset->blk_len; i += 4) {
		addr = regset->offset + i;
		if ((addr & 0xF) == 0x0)
			seq_printf(s, i ? "\n[%x]" : "[%x]", addr);
		seq_printf(s, " %08x", readl_relaxed(base + i));
	}
	seq_puts(s, "\n");
	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dpu_regset32);

void dpu_debugfs_create_regset32(const char *name, umode_t mode,
		void *parent,
		uint32_t offset, uint32_t length, struct dpu_kms *dpu_kms)
{
	struct dpu_debugfs_regset32 *regset;

	if (WARN_ON(!name || !dpu_kms || !length))
		return;

	regset = devm_kzalloc(&dpu_kms->pdev->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	/* make sure offset is a multiple of 4 */
	regset->offset = round_down(offset, 4);
	regset->blk_len = length;
	regset->dpu_kms = dpu_kms;

	debugfs_create_file(name, mode, parent, regset, &dpu_regset32_fops);
}

static void dpu_debugfs_sspp_init(struct dpu_kms *dpu_kms, struct dentry *debugfs_root)
{
	struct dentry *entry = debugfs_create_dir("sspp", debugfs_root);
	int i;

	if (IS_ERR(entry))
		return;

	for (i = SSPP_NONE; i < SSPP_MAX; i++) {
		struct dpu_hw_sspp *hw = dpu_rm_get_sspp(&dpu_kms->rm, i);

		if (!hw)
			continue;

		_dpu_hw_sspp_init_debugfs(hw, dpu_kms, entry);
	}
}

static int dpu_kms_debugfs_init(struct msm_kms *kms, struct drm_minor *minor)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	void *p = dpu_hw_util_get_log_mask_ptr();
	struct dentry *entry;

	if (!p)
		return -EINVAL;

	/* Only create a set of debugfs for the primary node, ignore render nodes */
	if (minor->type != DRM_MINOR_PRIMARY)
		return 0;

	entry = debugfs_create_dir("debug", minor->debugfs_root);

	debugfs_create_x32(DPU_DEBUGFS_HWMASKNAME, 0600, entry, p);

	dpu_debugfs_danger_init(dpu_kms, entry);
	dpu_debugfs_vbif_init(dpu_kms, entry);
	dpu_debugfs_core_irq_init(dpu_kms, entry);
	dpu_debugfs_sspp_init(dpu_kms, entry);

	return dpu_core_perf_debugfs_init(dpu_kms, entry);
}
#endif

/* Global/shared object state funcs */

/*
 * This is a helper that returns the private state currently in operation.
 * Note that this would return the "old_state" if called in the atomic check
 * path, and the "new_state" after the atomic swap has been done.
 */
struct dpu_global_state *
dpu_kms_get_existing_global_state(struct dpu_kms *dpu_kms)
{
	return to_dpu_global_state(dpu_kms->global_state.state);
}

/*
 * This acquires the modeset lock set aside for global state, creates
 * a new duplicated private object state.
 */
struct dpu_global_state *dpu_kms_get_global_state(struct drm_atomic_state *s)
{
	struct msm_drm_private *priv = s->dev->dev_private;
	struct dpu_kms *dpu_kms = to_dpu_kms(priv->kms);
	struct drm_private_state *priv_state;

	priv_state = drm_atomic_get_private_obj_state(s,
						&dpu_kms->global_state);
	if (IS_ERR(priv_state))
		return ERR_CAST(priv_state);

	return to_dpu_global_state(priv_state);
}

static struct drm_private_state *
dpu_kms_global_duplicate_state(struct drm_private_obj *obj)
{
	struct dpu_global_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void dpu_kms_global_destroy_state(struct drm_private_obj *obj,
				      struct drm_private_state *state)
{
	struct dpu_global_state *dpu_state = to_dpu_global_state(state);

	kfree(dpu_state);
}

static void dpu_kms_global_print_state(struct drm_printer *p,
				       const struct drm_private_state *state)
{
	const struct dpu_global_state *global_state = to_dpu_global_state(state);

	dpu_rm_print_state(p, global_state);
}

static const struct drm_private_state_funcs dpu_kms_global_state_funcs = {
	.atomic_duplicate_state = dpu_kms_global_duplicate_state,
	.atomic_destroy_state = dpu_kms_global_destroy_state,
	.atomic_print_state = dpu_kms_global_print_state,
};

static int dpu_kms_global_obj_init(struct dpu_kms *dpu_kms)
{
	struct dpu_global_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	drm_atomic_private_obj_init(dpu_kms->dev, &dpu_kms->global_state,
				    &state->base,
				    &dpu_kms_global_state_funcs);

	state->rm = &dpu_kms->rm;

	return 0;
}

static void dpu_kms_global_obj_fini(struct dpu_kms *dpu_kms)
{
	drm_atomic_private_obj_fini(&dpu_kms->global_state);
}

static int dpu_kms_parse_data_bus_icc_path(struct dpu_kms *dpu_kms)
{
	struct icc_path *path0;
	struct icc_path *path1;
	struct device *dpu_dev = &dpu_kms->pdev->dev;

	path0 = msm_icc_get(dpu_dev, "mdp0-mem");
	path1 = msm_icc_get(dpu_dev, "mdp1-mem");

	if (IS_ERR_OR_NULL(path0))
		return PTR_ERR_OR_ZERO(path0);

	dpu_kms->path[0] = path0;
	dpu_kms->num_paths = 1;

	if (!IS_ERR_OR_NULL(path1)) {
		dpu_kms->path[1] = path1;
		dpu_kms->num_paths++;
	}
	return 0;
}

static int dpu_kms_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	return dpu_crtc_vblank(crtc, true);
}

static void dpu_kms_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	dpu_crtc_vblank(crtc, false);
}

static void dpu_kms_enable_commit(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	pm_runtime_get_sync(&dpu_kms->pdev->dev);
}

static void dpu_kms_disable_commit(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);
}

static void dpu_kms_flush_commit(struct msm_kms *kms, unsigned crtc_mask)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	struct drm_crtc *crtc;

	for_each_crtc_mask(dpu_kms->dev, crtc, crtc_mask) {
		if (!crtc->state->active)
			continue;

		trace_dpu_kms_commit(DRMID(crtc));
		dpu_crtc_commit_kickoff(crtc);
	}
}

static void dpu_kms_complete_commit(struct msm_kms *kms, unsigned crtc_mask)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	struct drm_crtc *crtc;

	DPU_ATRACE_BEGIN("kms_complete_commit");

	for_each_crtc_mask(dpu_kms->dev, crtc, crtc_mask)
		dpu_crtc_complete_commit(crtc);

	DPU_ATRACE_END("kms_complete_commit");
}

static void dpu_kms_wait_for_commit_done(struct msm_kms *kms,
		struct drm_crtc *crtc)
{
	struct drm_encoder *encoder;
	struct drm_device *dev;
	int ret;

	if (!kms || !crtc || !crtc->state) {
		DPU_ERROR("invalid params\n");
		return;
	}

	dev = crtc->dev;

	if (!crtc->state->enable) {
		DPU_DEBUG("[crtc:%d] not enable\n", crtc->base.id);
		return;
	}

	if (!drm_atomic_crtc_effectively_active(crtc->state)) {
		DPU_DEBUG("[crtc:%d] not active\n", crtc->base.id);
		return;
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc != crtc)
			continue;
		/*
		 * Wait for post-flush if necessary to delay before
		 * plane_cleanup. For example, wait for vsync in case of video
		 * mode panels. This may be a no-op for command mode panels.
		 */
		trace_dpu_kms_wait_for_commit_done(DRMID(crtc));
		ret = dpu_encoder_wait_for_commit_done(encoder);
		if (ret && ret != -EWOULDBLOCK) {
			DPU_ERROR("wait for commit done returned %d\n", ret);
			break;
		}
	}
}

static void dpu_kms_wait_flush(struct msm_kms *kms, unsigned crtc_mask)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	struct drm_crtc *crtc;

	for_each_crtc_mask(dpu_kms->dev, crtc, crtc_mask)
		dpu_kms_wait_for_commit_done(kms, crtc);
}

static const char *dpu_vsync_sources[] = {
	[DPU_VSYNC_SOURCE_GPIO_0] = "mdp_vsync_p",
	[DPU_VSYNC_SOURCE_GPIO_1] = "mdp_vsync_s",
	[DPU_VSYNC_SOURCE_GPIO_2] = "mdp_vsync_e",
	[DPU_VSYNC_SOURCE_INTF_0] = "mdp_intf0",
	[DPU_VSYNC_SOURCE_INTF_1] = "mdp_intf1",
	[DPU_VSYNC_SOURCE_INTF_2] = "mdp_intf2",
	[DPU_VSYNC_SOURCE_INTF_3] = "mdp_intf3",
	[DPU_VSYNC_SOURCE_WD_TIMER_0] = "timer0",
	[DPU_VSYNC_SOURCE_WD_TIMER_1] = "timer1",
	[DPU_VSYNC_SOURCE_WD_TIMER_2] = "timer2",
	[DPU_VSYNC_SOURCE_WD_TIMER_3] = "timer3",
	[DPU_VSYNC_SOURCE_WD_TIMER_4] = "timer4",
};

static int dpu_kms_dsi_set_te_source(struct msm_display_info *info,
				     struct msm_dsi *dsi)
{
	const char *te_source = msm_dsi_get_te_source(dsi);
	int i;

	if (!te_source) {
		info->vsync_source = DPU_VSYNC_SOURCE_GPIO_0;
		return 0;
	}

	/* we can not use match_string since dpu_vsync_sources is a sparse array */
	for (i = 0; i < ARRAY_SIZE(dpu_vsync_sources); i++) {
		if (dpu_vsync_sources[i] &&
		    !strcmp(dpu_vsync_sources[i], te_source)) {
			info->vsync_source = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int _dpu_kms_initialize_dsi(struct drm_device *dev,
				    struct msm_drm_private *priv,
				    struct dpu_kms *dpu_kms)
{
	struct drm_encoder *encoder = NULL;
	struct msm_display_info info;
	int i, rc = 0;

	if (!(priv->dsi[0] || priv->dsi[1]))
		return rc;

	/*
	 * We support following confiurations:
	 * - Single DSI host (dsi0 or dsi1)
	 * - Two independent DSI hosts
	 * - Bonded DSI0 and DSI1 hosts
	 *
	 * TODO: Support swapping DSI0 and DSI1 in the bonded setup.
	 */
	for (i = 0; i < ARRAY_SIZE(priv->dsi); i++) {
		int other = (i + 1) % 2;

		if (!priv->dsi[i])
			continue;

		if (msm_dsi_is_bonded_dsi(priv->dsi[i]) &&
		    !msm_dsi_is_master_dsi(priv->dsi[i]))
			continue;

		memset(&info, 0, sizeof(info));
		info.intf_type = INTF_DSI;

		info.h_tile_instance[info.num_of_h_tiles++] = i;
		if (msm_dsi_is_bonded_dsi(priv->dsi[i]))
			info.h_tile_instance[info.num_of_h_tiles++] = other;

		info.is_cmd_mode = msm_dsi_is_cmd_mode(priv->dsi[i]);

		rc = dpu_kms_dsi_set_te_source(&info, priv->dsi[i]);
		if (rc) {
			DPU_ERROR("failed to identify TE source for dsi display\n");
			return rc;
		}

		encoder = dpu_encoder_init(dev, DRM_MODE_ENCODER_DSI, &info);
		if (IS_ERR(encoder)) {
			DPU_ERROR("encoder init failed for dsi display\n");
			return PTR_ERR(encoder);
		}

		rc = msm_dsi_modeset_init(priv->dsi[i], dev, encoder);
		if (rc) {
			DPU_ERROR("modeset_init failed for dsi[%d], rc = %d\n",
				i, rc);
			break;
		}

		if (msm_dsi_is_bonded_dsi(priv->dsi[i]) && priv->dsi[other]) {
			rc = msm_dsi_modeset_init(priv->dsi[other], dev, encoder);
			if (rc) {
				DPU_ERROR("modeset_init failed for dsi[%d], rc = %d\n",
					other, rc);
				break;
			}
		}
	}

	return rc;
}

static int _dpu_kms_initialize_displayport(struct drm_device *dev,
					    struct msm_drm_private *priv,
					    struct dpu_kms *dpu_kms)
{
	struct drm_encoder *encoder = NULL;
	struct msm_display_info info;
	bool yuv_supported;
	int rc;
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->dp); i++) {
		if (!priv->dp[i])
			continue;

		memset(&info, 0, sizeof(info));
		info.num_of_h_tiles = 1;
		info.h_tile_instance[0] = i;
		info.intf_type = INTF_DP;

		encoder = dpu_encoder_init(dev, DRM_MODE_ENCODER_TMDS, &info);
		if (IS_ERR(encoder)) {
			DPU_ERROR("encoder init failed for dsi display\n");
			return PTR_ERR(encoder);
		}

		yuv_supported = !!dpu_kms->catalog->cdm;
		rc = msm_dp_modeset_init(priv->dp[i], dev, encoder, yuv_supported);
		if (rc) {
			DPU_ERROR("modeset_init failed for DP, rc = %d\n", rc);
			return rc;
		}
	}

	return 0;
}

static int _dpu_kms_initialize_hdmi(struct drm_device *dev,
				    struct msm_drm_private *priv,
				    struct dpu_kms *dpu_kms)
{
	struct drm_encoder *encoder = NULL;
	struct msm_display_info info;
	int rc;

	if (!priv->hdmi)
		return 0;

	memset(&info, 0, sizeof(info));
	info.num_of_h_tiles = 1;
	info.h_tile_instance[0] = 0;
	info.intf_type = INTF_HDMI;

	encoder = dpu_encoder_init(dev, DRM_MODE_ENCODER_TMDS, &info);
	if (IS_ERR(encoder)) {
		DPU_ERROR("encoder init failed for HDMI display\n");
		return PTR_ERR(encoder);
	}

	rc = msm_hdmi_modeset_init(priv->hdmi, dev, encoder);
	if (rc) {
		DPU_ERROR("modeset_init failed for DP, rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int _dpu_kms_initialize_writeback(struct drm_device *dev,
		struct msm_drm_private *priv, struct dpu_kms *dpu_kms,
		const u32 *wb_formats, int n_formats)
{
	struct drm_encoder *encoder = NULL;
	struct msm_display_info info;
	const enum dpu_wb wb_idx = WB_2;
	u32 maxlinewidth;
	int rc;

	memset(&info, 0, sizeof(info));

	info.num_of_h_tiles = 1;
	/* use only WB idx 2 instance for DPU */
	info.h_tile_instance[0] = wb_idx;
	info.intf_type = INTF_WB;

	maxlinewidth = dpu_rm_get_wb(&dpu_kms->rm, info.h_tile_instance[0])->caps->maxlinewidth;

	encoder = dpu_encoder_init(dev, DRM_MODE_ENCODER_VIRTUAL, &info);
	if (IS_ERR(encoder)) {
		DPU_ERROR("encoder init failed for dsi display\n");
		return PTR_ERR(encoder);
	}

	rc = dpu_writeback_init(dev, encoder, wb_formats, n_formats, maxlinewidth);
	if (rc) {
		DPU_ERROR("dpu_writeback_init, rc = %d\n", rc);
		return rc;
	}

	return 0;
}

/**
 * _dpu_kms_setup_displays - create encoders, bridges and connectors
 *                           for underlying displays
 * @dev:        Pointer to drm device structure
 * @priv:       Pointer to private drm device data
 * @dpu_kms:    Pointer to dpu kms structure
 * Returns:     Zero on success
 */
static int _dpu_kms_setup_displays(struct drm_device *dev,
				    struct msm_drm_private *priv,
				    struct dpu_kms *dpu_kms)
{
	int rc = 0;
	int i;

	rc = _dpu_kms_initialize_dsi(dev, priv, dpu_kms);
	if (rc) {
		DPU_ERROR("initialize_dsi failed, rc = %d\n", rc);
		return rc;
	}

	rc = _dpu_kms_initialize_displayport(dev, priv, dpu_kms);
	if (rc) {
		DPU_ERROR("initialize_DP failed, rc = %d\n", rc);
		return rc;
	}

	rc = _dpu_kms_initialize_hdmi(dev, priv, dpu_kms);
	if (rc) {
		DPU_ERROR("initialize HDMI failed, rc = %d\n", rc);
		return rc;
	}

	/* Since WB isn't a driver check the catalog before initializing */
	if (dpu_kms->catalog->wb_count) {
		for (i = 0; i < dpu_kms->catalog->wb_count; i++) {
			if (dpu_kms->catalog->wb[i].id == WB_2) {
				rc = _dpu_kms_initialize_writeback(dev, priv, dpu_kms,
						dpu_kms->catalog->wb[i].format_list,
						dpu_kms->catalog->wb[i].num_formats);
				if (rc) {
					DPU_ERROR("initialize_WB failed, rc = %d\n", rc);
					return rc;
				}
			}
		}
	}

	return rc;
}

#define MAX_PLANES 20
static int _dpu_kms_drm_obj_init(struct dpu_kms *dpu_kms)
{
	struct drm_device *dev;
	struct drm_plane *primary_planes[MAX_PLANES], *plane;
	struct drm_plane *cursor_planes[MAX_PLANES] = { NULL };
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	unsigned int num_encoders;

	struct msm_drm_private *priv;
	const struct dpu_mdss_cfg *catalog;

	int primary_planes_idx = 0, cursor_planes_idx = 0, i, ret;
	int max_crtc_count;
	dev = dpu_kms->dev;
	priv = dev->dev_private;
	catalog = dpu_kms->catalog;

	/*
	 * Create encoder and query display drivers to create
	 * bridges and connectors
	 */
	ret = _dpu_kms_setup_displays(dev, priv, dpu_kms);
	if (ret)
		return ret;

	num_encoders = 0;
	drm_for_each_encoder(encoder, dev)
		num_encoders++;

	max_crtc_count = min(catalog->mixer_count, num_encoders);

	/* Create the planes, keeping track of one primary/cursor per crtc */
	for (i = 0; i < catalog->sspp_count; i++) {
		enum drm_plane_type type;

		if ((catalog->sspp[i].features & BIT(DPU_SSPP_CURSOR))
			&& cursor_planes_idx < max_crtc_count)
			type = DRM_PLANE_TYPE_CURSOR;
		else if (primary_planes_idx < max_crtc_count)
			type = DRM_PLANE_TYPE_PRIMARY;
		else
			type = DRM_PLANE_TYPE_OVERLAY;

		DPU_DEBUG("Create plane type %d with features %lx (cur %lx)\n",
			  type, catalog->sspp[i].features,
			  catalog->sspp[i].features & BIT(DPU_SSPP_CURSOR));

		plane = dpu_plane_init(dev, catalog->sspp[i].id, type,
				       (1UL << max_crtc_count) - 1);
		if (IS_ERR(plane)) {
			DPU_ERROR("dpu_plane_init failed\n");
			ret = PTR_ERR(plane);
			return ret;
		}

		if (type == DRM_PLANE_TYPE_CURSOR)
			cursor_planes[cursor_planes_idx++] = plane;
		else if (type == DRM_PLANE_TYPE_PRIMARY)
			primary_planes[primary_planes_idx++] = plane;
	}

	max_crtc_count = min(max_crtc_count, primary_planes_idx);

	/* Create one CRTC per encoder */
	for (i = 0; i < max_crtc_count; i++) {
		crtc = dpu_crtc_init(dev, primary_planes[i], cursor_planes[i]);
		if (IS_ERR(crtc)) {
			ret = PTR_ERR(crtc);
			return ret;
		}
		priv->num_crtcs++;
	}

	/* All CRTCs are compatible with all encoders */
	drm_for_each_encoder(encoder, dev)
		encoder->possible_crtcs = (1 << priv->num_crtcs) - 1;

	return 0;
}

static void _dpu_kms_hw_destroy(struct dpu_kms *dpu_kms)
{
	int i;

	dpu_kms->hw_intr = NULL;

	/* safe to call these more than once during shutdown */
	_dpu_kms_mmu_destroy(dpu_kms);

	for (i = 0; i < ARRAY_SIZE(dpu_kms->hw_vbif); i++) {
		dpu_kms->hw_vbif[i] = NULL;
	}

	dpu_kms_global_obj_fini(dpu_kms);

	dpu_kms->catalog = NULL;

	dpu_kms->hw_mdp = NULL;
}

static void dpu_kms_destroy(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms;

	if (!kms) {
		DPU_ERROR("invalid kms\n");
		return;
	}

	dpu_kms = to_dpu_kms(kms);

	_dpu_kms_hw_destroy(dpu_kms);

	msm_kms_destroy(&dpu_kms->base);

	if (dpu_kms->rpm_enabled)
		pm_runtime_disable(&dpu_kms->pdev->dev);
}

static int dpu_irq_postinstall(struct msm_kms *kms)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);

	if (!dpu_kms || !dpu_kms->dev)
		return -EINVAL;

	priv = dpu_kms->dev->dev_private;
	if (!priv)
		return -EINVAL;

	return 0;
}

static void dpu_kms_mdp_snapshot(struct msm_disp_state *disp_state, struct msm_kms *kms)
{
	int i;
	struct dpu_kms *dpu_kms;
	const struct dpu_mdss_cfg *cat;
	void __iomem *base;

	dpu_kms = to_dpu_kms(kms);

	cat = dpu_kms->catalog;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);

	/* dump CTL sub-blocks HW regs info */
	for (i = 0; i < cat->ctl_count; i++)
		msm_disp_snapshot_add_block(disp_state, cat->ctl[i].len,
				dpu_kms->mmio + cat->ctl[i].base, cat->ctl[i].name);

	/* dump DSPP sub-blocks HW regs info */
	for (i = 0; i < cat->dspp_count; i++) {
		base = dpu_kms->mmio + cat->dspp[i].base;
		msm_disp_snapshot_add_block(disp_state, cat->dspp[i].len, base, cat->dspp[i].name);

		if (cat->dspp[i].sblk && cat->dspp[i].sblk->pcc.len > 0)
			msm_disp_snapshot_add_block(disp_state, cat->dspp[i].sblk->pcc.len,
						    base + cat->dspp[i].sblk->pcc.base, "%s_%s",
						    cat->dspp[i].name,
						    cat->dspp[i].sblk->pcc.name);
	}

	/* dump INTF sub-blocks HW regs info */
	for (i = 0; i < cat->intf_count; i++)
		msm_disp_snapshot_add_block(disp_state, cat->intf[i].len,
				dpu_kms->mmio + cat->intf[i].base, cat->intf[i].name);

	/* dump PP sub-blocks HW regs info */
	for (i = 0; i < cat->pingpong_count; i++) {
		base = dpu_kms->mmio + cat->pingpong[i].base;
		msm_disp_snapshot_add_block(disp_state, cat->pingpong[i].len, base,
					    cat->pingpong[i].name);

		/* TE2 sub-block has length of 0, so will not print it */

		if (cat->pingpong[i].sblk && cat->pingpong[i].sblk->dither.len > 0)
			msm_disp_snapshot_add_block(disp_state, cat->pingpong[i].sblk->dither.len,
						    base + cat->pingpong[i].sblk->dither.base,
						    "%s_%s", cat->pingpong[i].name,
						    cat->pingpong[i].sblk->dither.name);
	}

	/* dump SSPP sub-blocks HW regs info */
	for (i = 0; i < cat->sspp_count; i++) {
		base = dpu_kms->mmio + cat->sspp[i].base;
		msm_disp_snapshot_add_block(disp_state, cat->sspp[i].len, base, cat->sspp[i].name);

		if (cat->sspp[i].sblk && cat->sspp[i].sblk->scaler_blk.len > 0)
			msm_disp_snapshot_add_block(disp_state, cat->sspp[i].sblk->scaler_blk.len,
						    base + cat->sspp[i].sblk->scaler_blk.base,
						    "%s_%s", cat->sspp[i].name,
						    cat->sspp[i].sblk->scaler_blk.name);

		if (cat->sspp[i].sblk && cat->sspp[i].sblk->csc_blk.len > 0)
			msm_disp_snapshot_add_block(disp_state, cat->sspp[i].sblk->csc_blk.len,
						    base + cat->sspp[i].sblk->csc_blk.base,
						    "%s_%s", cat->sspp[i].name,
						    cat->sspp[i].sblk->csc_blk.name);
	}

	/* dump LM sub-blocks HW regs info */
	for (i = 0; i < cat->mixer_count; i++)
		msm_disp_snapshot_add_block(disp_state, cat->mixer[i].len,
				dpu_kms->mmio + cat->mixer[i].base, cat->mixer[i].name);

	/* dump WB sub-blocks HW regs info */
	for (i = 0; i < cat->wb_count; i++)
		msm_disp_snapshot_add_block(disp_state, cat->wb[i].len,
				dpu_kms->mmio + cat->wb[i].base, cat->wb[i].name);

	if (cat->mdp[0].features & BIT(DPU_MDP_PERIPH_0_REMOVED)) {
		msm_disp_snapshot_add_block(disp_state, MDP_PERIPH_TOP0,
				dpu_kms->mmio + cat->mdp[0].base, "top");
		msm_disp_snapshot_add_block(disp_state, cat->mdp[0].len - MDP_PERIPH_TOP0_END,
				dpu_kms->mmio + cat->mdp[0].base + MDP_PERIPH_TOP0_END, "top_2");
	} else {
		msm_disp_snapshot_add_block(disp_state, cat->mdp[0].len,
				dpu_kms->mmio + cat->mdp[0].base, "top");
	}

	/* dump DSC sub-blocks HW regs info */
	for (i = 0; i < cat->dsc_count; i++) {
		base = dpu_kms->mmio + cat->dsc[i].base;
		msm_disp_snapshot_add_block(disp_state, cat->dsc[i].len, base, cat->dsc[i].name);

		if (cat->dsc[i].features & BIT(DPU_DSC_HW_REV_1_2)) {
			struct dpu_dsc_blk enc = cat->dsc[i].sblk->enc;
			struct dpu_dsc_blk ctl = cat->dsc[i].sblk->ctl;

			msm_disp_snapshot_add_block(disp_state, enc.len, base + enc.base, "%s_%s",
						    cat->dsc[i].name, enc.name);
			msm_disp_snapshot_add_block(disp_state, ctl.len, base + ctl.base, "%s_%s",
						    cat->dsc[i].name, ctl.name);
		}
	}

	if (cat->cdm)
		msm_disp_snapshot_add_block(disp_state, cat->cdm->len,
					    dpu_kms->mmio + cat->cdm->base, cat->cdm->name);

	pm_runtime_put_sync(&dpu_kms->pdev->dev);
}

static const struct msm_kms_funcs kms_funcs = {
	.hw_init         = dpu_kms_hw_init,
	.irq_preinstall  = dpu_core_irq_preinstall,
	.irq_postinstall = dpu_irq_postinstall,
	.irq_uninstall   = dpu_core_irq_uninstall,
	.irq             = dpu_core_irq,
	.enable_commit   = dpu_kms_enable_commit,
	.disable_commit  = dpu_kms_disable_commit,
	.flush_commit    = dpu_kms_flush_commit,
	.wait_flush      = dpu_kms_wait_flush,
	.complete_commit = dpu_kms_complete_commit,
	.enable_vblank   = dpu_kms_enable_vblank,
	.disable_vblank  = dpu_kms_disable_vblank,
	.check_modified_format = dpu_format_check_modified_format,
	.destroy         = dpu_kms_destroy,
	.snapshot        = dpu_kms_mdp_snapshot,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init    = dpu_kms_debugfs_init,
#endif
};

static void _dpu_kms_mmu_destroy(struct dpu_kms *dpu_kms)
{
	struct msm_mmu *mmu;

	if (!dpu_kms->base.aspace)
		return;

	mmu = dpu_kms->base.aspace->mmu;

	mmu->funcs->detach(mmu);
	msm_gem_address_space_put(dpu_kms->base.aspace);

	dpu_kms->base.aspace = NULL;
}

static int _dpu_kms_mmu_init(struct dpu_kms *dpu_kms)
{
	struct msm_gem_address_space *aspace;

	aspace = msm_kms_init_aspace(dpu_kms->dev);
	if (IS_ERR(aspace))
		return PTR_ERR(aspace);

	dpu_kms->base.aspace = aspace;

	return 0;
}

unsigned long dpu_kms_get_clk_rate(struct dpu_kms *dpu_kms, char *clock_name)
{
	struct clk *clk;

	clk = msm_clk_bulk_get_clock(dpu_kms->clocks, dpu_kms->num_clocks, clock_name);
	if (!clk)
		return 0;

	return clk_get_rate(clk);
}

#define	DPU_PERF_DEFAULT_MAX_CORE_CLK_RATE	412500000

static int dpu_kms_hw_init(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms;
	struct drm_device *dev;
	int i, rc = -EINVAL;
	unsigned long max_core_clk_rate;
	u32 core_rev;

	if (!kms) {
		DPU_ERROR("invalid kms\n");
		return rc;
	}

	dpu_kms = to_dpu_kms(kms);
	dev = dpu_kms->dev;

	dev->mode_config.cursor_width = 512;
	dev->mode_config.cursor_height = 512;

	rc = dpu_kms_global_obj_init(dpu_kms);
	if (rc)
		return rc;

	atomic_set(&dpu_kms->bandwidth_ref, 0);

	rc = pm_runtime_resume_and_get(&dpu_kms->pdev->dev);
	if (rc < 0)
		goto error;

	core_rev = readl_relaxed(dpu_kms->mmio + 0x0);

	pr_info("dpu hardware revision:0x%x\n", core_rev);

	dpu_kms->catalog = of_device_get_match_data(dev->dev);
	if (!dpu_kms->catalog) {
		DPU_ERROR("device config not known!\n");
		rc = -EINVAL;
		goto err_pm_put;
	}

	/*
	 * Now we need to read the HW catalog and initialize resources such as
	 * clocks, regulators, GDSC/MMAGIC, ioremap the register ranges etc
	 */
	rc = _dpu_kms_mmu_init(dpu_kms);
	if (rc) {
		DPU_ERROR("dpu_kms_mmu_init failed: %d\n", rc);
		goto err_pm_put;
	}

	dpu_kms->mdss = msm_mdss_get_mdss_data(dpu_kms->pdev->dev.parent);
	if (IS_ERR(dpu_kms->mdss)) {
		rc = PTR_ERR(dpu_kms->mdss);
		DPU_ERROR("failed to get MDSS data: %d\n", rc);
		goto err_pm_put;
	}

	if (!dpu_kms->mdss) {
		rc = -EINVAL;
		DPU_ERROR("NULL MDSS data\n");
		goto err_pm_put;
	}

	rc = dpu_rm_init(dev, &dpu_kms->rm, dpu_kms->catalog, dpu_kms->mdss, dpu_kms->mmio);
	if (rc) {
		DPU_ERROR("rm init failed: %d\n", rc);
		goto err_pm_put;
	}

	dpu_kms->hw_mdp = dpu_hw_mdptop_init(dev,
					     dpu_kms->catalog->mdp,
					     dpu_kms->mmio,
					     dpu_kms->catalog);
	if (IS_ERR(dpu_kms->hw_mdp)) {
		rc = PTR_ERR(dpu_kms->hw_mdp);
		DPU_ERROR("failed to get hw_mdp: %d\n", rc);
		dpu_kms->hw_mdp = NULL;
		goto err_pm_put;
	}

	for (i = 0; i < dpu_kms->catalog->vbif_count; i++) {
		struct dpu_hw_vbif *hw;
		const struct dpu_vbif_cfg *vbif = &dpu_kms->catalog->vbif[i];

		hw = dpu_hw_vbif_init(dev, vbif, dpu_kms->vbif[vbif->id]);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed to init vbif %d: %d\n", vbif->id, rc);
			goto err_pm_put;
		}

		dpu_kms->hw_vbif[vbif->id] = hw;
	}

	/* TODO: use the same max_freq as in dpu_kms_hw_init */
	max_core_clk_rate = dpu_kms_get_clk_rate(dpu_kms, "core");
	if (!max_core_clk_rate) {
		DPU_DEBUG("max core clk rate not determined, using default\n");
		max_core_clk_rate = DPU_PERF_DEFAULT_MAX_CORE_CLK_RATE;
	}

	rc = dpu_core_perf_init(&dpu_kms->perf, dpu_kms->catalog->perf, max_core_clk_rate);
	if (rc) {
		DPU_ERROR("failed to init perf %d\n", rc);
		goto err_pm_put;
	}

	dpu_kms->hw_intr = dpu_hw_intr_init(dev, dpu_kms->mmio, dpu_kms->catalog);
	if (IS_ERR(dpu_kms->hw_intr)) {
		rc = PTR_ERR(dpu_kms->hw_intr);
		DPU_ERROR("hw_intr init failed: %d\n", rc);
		dpu_kms->hw_intr = NULL;
		goto err_pm_put;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	/*
	 * max crtc width is equal to the max mixer width * 2 and max height is
	 * is 4K
	 */
	dev->mode_config.max_width =
			dpu_kms->catalog->caps->max_mixer_width * 2;
	dev->mode_config.max_height = 4096;

	dev->max_vblank_count = 0xffffffff;
	/* Disable vblank irqs aggressively for power-saving */
	dev->vblank_disable_immediate = true;

	/*
	 * _dpu_kms_drm_obj_init should create the DRM related objects
	 * i.e. CRTCs, planes, encoders, connectors and so forth
	 */
	rc = _dpu_kms_drm_obj_init(dpu_kms);
	if (rc) {
		DPU_ERROR("modeset init failed: %d\n", rc);
		goto err_pm_put;
	}

	dpu_vbif_init_memtypes(dpu_kms);

	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	return 0;

err_pm_put:
	pm_runtime_put_sync(&dpu_kms->pdev->dev);
error:
	_dpu_kms_hw_destroy(dpu_kms);

	return rc;
}

static int dpu_kms_init(struct drm_device *ddev)
{
	struct msm_drm_private *priv = ddev->dev_private;
	struct device *dev = ddev->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_kms *dpu_kms = to_dpu_kms(priv->kms);
	struct dev_pm_opp *opp;
	int ret = 0;
	unsigned long max_freq = ULONG_MAX;

	opp = dev_pm_opp_find_freq_floor(dev, &max_freq);
	if (!IS_ERR(opp))
		dev_pm_opp_put(opp);

	dev_pm_opp_set_rate(dev, max_freq);

	ret = msm_kms_init(&dpu_kms->base, &kms_funcs);
	if (ret) {
		DPU_ERROR("failed to init kms, ret=%d\n", ret);
		return ret;
	}
	dpu_kms->dev = ddev;

	pm_runtime_enable(&pdev->dev);
	dpu_kms->rpm_enabled = true;

	return 0;
}

static int dpu_kms_mmap_mdp5(struct dpu_kms *dpu_kms)
{
	struct platform_device *pdev = dpu_kms->pdev;
	struct platform_device *mdss_dev;
	int ret;

	if (!dev_is_platform(dpu_kms->pdev->dev.parent))
		return -EINVAL;

	mdss_dev = to_platform_device(dpu_kms->pdev->dev.parent);

	dpu_kms->mmio = msm_ioremap(pdev, "mdp_phys");
	if (IS_ERR(dpu_kms->mmio)) {
		ret = PTR_ERR(dpu_kms->mmio);
		DPU_ERROR("mdp register memory map failed: %d\n", ret);
		dpu_kms->mmio = NULL;
		return ret;
	}
	DRM_DEBUG("mapped dpu address space @%pK\n", dpu_kms->mmio);

	dpu_kms->vbif[VBIF_RT] = msm_ioremap_mdss(mdss_dev,
						  dpu_kms->pdev,
						  "vbif_phys");
	if (IS_ERR(dpu_kms->vbif[VBIF_RT])) {
		ret = PTR_ERR(dpu_kms->vbif[VBIF_RT]);
		DPU_ERROR("vbif register memory map failed: %d\n", ret);
		dpu_kms->vbif[VBIF_RT] = NULL;
		return ret;
	}

	dpu_kms->vbif[VBIF_NRT] = msm_ioremap_mdss(mdss_dev,
						   dpu_kms->pdev,
						   "vbif_nrt_phys");
	if (IS_ERR(dpu_kms->vbif[VBIF_NRT])) {
		dpu_kms->vbif[VBIF_NRT] = NULL;
		DPU_DEBUG("VBIF NRT is not defined");
	}

	return 0;
}

static int dpu_kms_mmap_dpu(struct dpu_kms *dpu_kms)
{
	struct platform_device *pdev = dpu_kms->pdev;
	int ret;

	dpu_kms->mmio = msm_ioremap(pdev, "mdp");
	if (IS_ERR(dpu_kms->mmio)) {
		ret = PTR_ERR(dpu_kms->mmio);
		DPU_ERROR("mdp register memory map failed: %d\n", ret);
		dpu_kms->mmio = NULL;
		return ret;
	}
	DRM_DEBUG("mapped dpu address space @%pK\n", dpu_kms->mmio);

	dpu_kms->vbif[VBIF_RT] = msm_ioremap(pdev, "vbif");
	if (IS_ERR(dpu_kms->vbif[VBIF_RT])) {
		ret = PTR_ERR(dpu_kms->vbif[VBIF_RT]);
		DPU_ERROR("vbif register memory map failed: %d\n", ret);
		dpu_kms->vbif[VBIF_RT] = NULL;
		return ret;
	}

	dpu_kms->vbif[VBIF_NRT] = msm_ioremap_quiet(pdev, "vbif_nrt");
	if (IS_ERR(dpu_kms->vbif[VBIF_NRT])) {
		dpu_kms->vbif[VBIF_NRT] = NULL;
		DPU_DEBUG("VBIF NRT is not defined");
	}

	return 0;
}

static int dpu_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dpu_kms *dpu_kms;
	int irq;
	int ret = 0;

	if (!msm_disp_drv_should_bind(&pdev->dev, true))
		return -ENODEV;

	dpu_kms = devm_kzalloc(dev, sizeof(*dpu_kms), GFP_KERNEL);
	if (!dpu_kms)
		return -ENOMEM;

	dpu_kms->pdev = pdev;

	ret = devm_pm_opp_set_clkname(dev, "core");
	if (ret)
		return ret;
	/* OPP table is optional */
	ret = devm_pm_opp_of_add_table(dev);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "invalid OPP table in device tree\n");

	ret = devm_clk_bulk_get_all(&pdev->dev, &dpu_kms->clocks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to parse clocks\n");

	dpu_kms->num_clocks = ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return dev_err_probe(dev, irq, "failed to get irq\n");

	dpu_kms->base.irq = irq;

	if (of_device_is_compatible(dpu_kms->pdev->dev.of_node, "qcom,mdp5"))
		ret = dpu_kms_mmap_mdp5(dpu_kms);
	else
		ret = dpu_kms_mmap_dpu(dpu_kms);
	if (ret)
		return ret;

	ret = dpu_kms_parse_data_bus_icc_path(dpu_kms);
	if (ret)
		return ret;

	return msm_drv_probe(&pdev->dev, dpu_kms_init, &dpu_kms->base);
}

static void dpu_dev_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &msm_drm_ops);
}

static int __maybe_unused dpu_runtime_suspend(struct device *dev)
{
	int i;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct dpu_kms *dpu_kms = to_dpu_kms(priv->kms);

	/* Drop the performance state vote */
	dev_pm_opp_set_rate(dev, 0);
	clk_bulk_disable_unprepare(dpu_kms->num_clocks, dpu_kms->clocks);

	for (i = 0; i < dpu_kms->num_paths; i++)
		icc_set_bw(dpu_kms->path[i], 0, 0);

	return 0;
}

static int __maybe_unused dpu_runtime_resume(struct device *dev)
{
	int rc = -1;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct dpu_kms *dpu_kms = to_dpu_kms(priv->kms);
	struct drm_encoder *encoder;
	struct drm_device *ddev;

	ddev = dpu_kms->dev;

	rc = clk_bulk_prepare_enable(dpu_kms->num_clocks, dpu_kms->clocks);
	if (rc) {
		DPU_ERROR("clock enable failed rc:%d\n", rc);
		return rc;
	}

	dpu_vbif_init_memtypes(dpu_kms);

	drm_for_each_encoder(encoder, ddev)
		dpu_encoder_virt_runtime_resume(encoder);

	return rc;
}

static const struct dev_pm_ops dpu_pm_ops = {
	SET_RUNTIME_PM_OPS(dpu_runtime_suspend, dpu_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	.prepare = msm_kms_pm_prepare,
	.complete = msm_kms_pm_complete,
};

static const struct of_device_id dpu_dt_match[] = {
	{ .compatible = "qcom,msm8998-dpu", .data = &dpu_msm8998_cfg, },
	{ .compatible = "qcom,qcm2290-dpu", .data = &dpu_qcm2290_cfg, },
	{ .compatible = "qcom,sdm630-mdp5", .data = &dpu_sdm630_cfg, },
	{ .compatible = "qcom,sdm660-mdp5", .data = &dpu_sdm660_cfg, },
	{ .compatible = "qcom,sdm670-dpu", .data = &dpu_sdm670_cfg, },
	{ .compatible = "qcom,sdm845-dpu", .data = &dpu_sdm845_cfg, },
	{ .compatible = "qcom,sc7180-dpu", .data = &dpu_sc7180_cfg, },
	{ .compatible = "qcom,sc7280-dpu", .data = &dpu_sc7280_cfg, },
	{ .compatible = "qcom,sc8180x-dpu", .data = &dpu_sc8180x_cfg, },
	{ .compatible = "qcom,sc8280xp-dpu", .data = &dpu_sc8280xp_cfg, },
	{ .compatible = "qcom,sm6115-dpu", .data = &dpu_sm6115_cfg, },
	{ .compatible = "qcom,sm6125-dpu", .data = &dpu_sm6125_cfg, },
	{ .compatible = "qcom,sm6350-dpu", .data = &dpu_sm6350_cfg, },
	{ .compatible = "qcom,sm6375-dpu", .data = &dpu_sm6375_cfg, },
	{ .compatible = "qcom,sm7150-dpu", .data = &dpu_sm7150_cfg, },
	{ .compatible = "qcom,sm8150-dpu", .data = &dpu_sm8150_cfg, },
	{ .compatible = "qcom,sm8250-dpu", .data = &dpu_sm8250_cfg, },
	{ .compatible = "qcom,sm8350-dpu", .data = &dpu_sm8350_cfg, },
	{ .compatible = "qcom,sm8450-dpu", .data = &dpu_sm8450_cfg, },
	{ .compatible = "qcom,sm8550-dpu", .data = &dpu_sm8550_cfg, },
	{ .compatible = "qcom,sm8650-dpu", .data = &dpu_sm8650_cfg, },
	{ .compatible = "qcom,x1e80100-dpu", .data = &dpu_x1e80100_cfg, },
	{}
};
MODULE_DEVICE_TABLE(of, dpu_dt_match);

static struct platform_driver dpu_driver = {
	.probe = dpu_dev_probe,
	.remove_new = dpu_dev_remove,
	.shutdown = msm_kms_shutdown,
	.driver = {
		.name = "msm_dpu",
		.of_match_table = dpu_dt_match,
		.pm = &dpu_pm_ops,
	},
};

void __init msm_dpu_register(void)
{
	platform_driver_register(&dpu_driver);
}

void __exit msm_dpu_unregister(void)
{
	platform_driver_unregister(&dpu_driver);
}
