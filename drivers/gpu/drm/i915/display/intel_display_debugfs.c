// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_debugfs.h>
#include <drm/drm_fourcc.h>

#include "i915_debugfs.h"
#include "intel_de.h"
#include "intel_display_debugfs.h"
#include "intel_display_power.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dp.h"
#include "intel_dp_mst.h"
#include "intel_drrs.h"
#include "intel_fbc.h"
#include "intel_hdcp.h"
#include "intel_hdmi.h"
#include "intel_pm.h"
#include "intel_psr.h"
#include "intel_sprite.h"

static inline struct drm_i915_private *node_to_i915(struct drm_info_node *node)
{
	return to_i915(node->minor->dev);
}

static int i915_frontbuffer_tracking(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);

	seq_printf(m, "FB tracking busy bits: 0x%08x\n",
		   dev_priv->fb_tracking.busy_bits);

	seq_printf(m, "FB tracking flip bits: 0x%08x\n",
		   dev_priv->fb_tracking.flip_bits);

	return 0;
}

static int i915_ips_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	intel_wakeref_t wakeref;

	if (!HAS_IPS(dev_priv))
		return -ENODEV;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	seq_printf(m, "Enabled by kernel parameter: %s\n",
		   yesno(dev_priv->params.enable_ips));

	if (DISPLAY_VER(dev_priv) >= 8) {
		seq_puts(m, "Currently: unknown\n");
	} else {
		if (intel_de_read(dev_priv, IPS_CTL) & IPS_ENABLE)
			seq_puts(m, "Currently: enabled\n");
		else
			seq_puts(m, "Currently: disabled\n");
	}

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static int i915_sr_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	intel_wakeref_t wakeref;
	bool sr_enabled = false;

	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);

	if (DISPLAY_VER(dev_priv) >= 9)
		/* no global SR status; inspect per-plane WM */;
	else if (HAS_PCH_SPLIT(dev_priv))
		sr_enabled = intel_de_read(dev_priv, WM1_LP_ILK) & WM1_LP_SR_EN;
	else if (IS_I965GM(dev_priv) || IS_G4X(dev_priv) ||
		 IS_I945G(dev_priv) || IS_I945GM(dev_priv))
		sr_enabled = intel_de_read(dev_priv, FW_BLC_SELF) & FW_BLC_SELF_EN;
	else if (IS_I915GM(dev_priv))
		sr_enabled = intel_de_read(dev_priv, INSTPM) & INSTPM_SELF_EN;
	else if (IS_PINEVIEW(dev_priv))
		sr_enabled = intel_de_read(dev_priv, DSPFW3) & PINEVIEW_SELF_REFRESH_EN;
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		sr_enabled = intel_de_read(dev_priv, FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;

	intel_display_power_put(dev_priv, POWER_DOMAIN_INIT, wakeref);

	seq_printf(m, "self-refresh: %s\n", enableddisabled(sr_enabled));

	return 0;
}

static int i915_opregion(struct seq_file *m, void *unused)
{
	struct intel_opregion *opregion = &node_to_i915(m->private)->opregion;

	if (opregion->header)
		seq_write(m, opregion->header, OPREGION_SIZE);

	return 0;
}

static int i915_vbt(struct seq_file *m, void *unused)
{
	struct intel_opregion *opregion = &node_to_i915(m->private)->opregion;

	if (opregion->vbt)
		seq_write(m, opregion->vbt, opregion->vbt_size);

	return 0;
}

static int i915_gem_framebuffer_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_framebuffer *fbdev_fb = NULL;
	struct drm_framebuffer *drm_fb;

#ifdef CONFIG_DRM_FBDEV_EMULATION
	if (dev_priv->fbdev && dev_priv->fbdev->helper.fb) {
		fbdev_fb = to_intel_framebuffer(dev_priv->fbdev->helper.fb);

		seq_printf(m, "fbcon size: %d x %d, depth %d, %d bpp, modifier 0x%llx, refcount %d, obj ",
			   fbdev_fb->base.width,
			   fbdev_fb->base.height,
			   fbdev_fb->base.format->depth,
			   fbdev_fb->base.format->cpp[0] * 8,
			   fbdev_fb->base.modifier,
			   drm_framebuffer_read_refcount(&fbdev_fb->base));
		i915_debugfs_describe_obj(m, intel_fb_obj(&fbdev_fb->base));
		seq_putc(m, '\n');
	}
#endif

	mutex_lock(&dev->mode_config.fb_lock);
	drm_for_each_fb(drm_fb, dev) {
		struct intel_framebuffer *fb = to_intel_framebuffer(drm_fb);
		if (fb == fbdev_fb)
			continue;

		seq_printf(m, "user size: %d x %d, depth %d, %d bpp, modifier 0x%llx, refcount %d, obj ",
			   fb->base.width,
			   fb->base.height,
			   fb->base.format->depth,
			   fb->base.format->cpp[0] * 8,
			   fb->base.modifier,
			   drm_framebuffer_read_refcount(&fb->base));
		i915_debugfs_describe_obj(m, intel_fb_obj(&fb->base));
		seq_putc(m, '\n');
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}

static int i915_psr_sink_status_show(struct seq_file *m, void *data)
{
	u8 val;
	static const char * const sink_status[] = {
		"inactive",
		"transition to active, capture and display",
		"active, display from RFB",
		"active, capture and display on sink device timings",
		"transition to inactive, capture and display, timing re-sync",
		"reserved",
		"reserved",
		"sink internal error",
	};
	struct drm_connector *connector = m->private;
	struct intel_dp *intel_dp =
		intel_attached_dp(to_intel_connector(connector));
	int ret;

	if (!CAN_PSR(intel_dp)) {
		seq_puts(m, "PSR Unsupported\n");
		return -ENODEV;
	}

	if (connector->status != connector_status_connected)
		return -ENODEV;

	ret = drm_dp_dpcd_readb(&intel_dp->aux, DP_PSR_STATUS, &val);

	if (ret == 1) {
		const char *str = "unknown";

		val &= DP_PSR_SINK_STATE_MASK;
		if (val < ARRAY_SIZE(sink_status))
			str = sink_status[val];
		seq_printf(m, "Sink PSR status: 0x%x [%s]\n", val, str);
	} else {
		return ret;
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_psr_sink_status);

static void
psr_source_status(struct intel_dp *intel_dp, struct seq_file *m)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	const char *status = "unknown";
	u32 val, status_val;

	if (intel_dp->psr.psr2_enabled) {
		static const char * const live_status[] = {
			"IDLE",
			"CAPTURE",
			"CAPTURE_FS",
			"SLEEP",
			"BUFON_FW",
			"ML_UP",
			"SU_STANDBY",
			"FAST_SLEEP",
			"DEEP_SLEEP",
			"BUF_ON",
			"TG_ON"
		};
		val = intel_de_read(dev_priv,
				    EDP_PSR2_STATUS(intel_dp->psr.transcoder));
		status_val = REG_FIELD_GET(EDP_PSR2_STATUS_STATE_MASK, val);
		if (status_val < ARRAY_SIZE(live_status))
			status = live_status[status_val];
	} else {
		static const char * const live_status[] = {
			"IDLE",
			"SRDONACK",
			"SRDENT",
			"BUFOFF",
			"BUFON",
			"AUXACK",
			"SRDOFFACK",
			"SRDENT_ON",
		};
		val = intel_de_read(dev_priv,
				    EDP_PSR_STATUS(intel_dp->psr.transcoder));
		status_val = (val & EDP_PSR_STATUS_STATE_MASK) >>
			      EDP_PSR_STATUS_STATE_SHIFT;
		if (status_val < ARRAY_SIZE(live_status))
			status = live_status[status_val];
	}

	seq_printf(m, "Source PSR status: %s [0x%08x]\n", status, val);
}

static int intel_psr_status(struct seq_file *m, struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_psr *psr = &intel_dp->psr;
	intel_wakeref_t wakeref;
	const char *status;
	bool enabled;
	u32 val;

	seq_printf(m, "Sink support: %s", yesno(psr->sink_support));
	if (psr->sink_support)
		seq_printf(m, " [0x%02x]", intel_dp->psr_dpcd[0]);
	seq_puts(m, "\n");

	if (!psr->sink_support)
		return 0;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);
	mutex_lock(&psr->lock);

	if (psr->enabled)
		status = psr->psr2_enabled ? "PSR2 enabled" : "PSR1 enabled";
	else
		status = "disabled";
	seq_printf(m, "PSR mode: %s\n", status);

	if (!psr->enabled) {
		seq_printf(m, "PSR sink not reliable: %s\n",
			   yesno(psr->sink_not_reliable));

		goto unlock;
	}

	if (psr->psr2_enabled) {
		val = intel_de_read(dev_priv,
				    EDP_PSR2_CTL(intel_dp->psr.transcoder));
		enabled = val & EDP_PSR2_ENABLE;
	} else {
		val = intel_de_read(dev_priv,
				    EDP_PSR_CTL(intel_dp->psr.transcoder));
		enabled = val & EDP_PSR_ENABLE;
	}
	seq_printf(m, "Source PSR ctl: %s [0x%08x]\n",
		   enableddisabled(enabled), val);
	psr_source_status(intel_dp, m);
	seq_printf(m, "Busy frontbuffer bits: 0x%08x\n",
		   psr->busy_frontbuffer_bits);

	/*
	 * SKL+ Perf counter is reset to 0 everytime DC state is entered
	 */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		val = intel_de_read(dev_priv,
				    EDP_PSR_PERF_CNT(intel_dp->psr.transcoder));
		val &= EDP_PSR_PERF_CNT_MASK;
		seq_printf(m, "Performance counter: %u\n", val);
	}

	if (psr->debug & I915_PSR_DEBUG_IRQ) {
		seq_printf(m, "Last attempted entry at: %lld\n",
			   psr->last_entry_attempt);
		seq_printf(m, "Last exit at: %lld\n", psr->last_exit);
	}

	if (psr->psr2_enabled) {
		u32 su_frames_val[3];
		int frame;

		/*
		 * Reading all 3 registers before hand to minimize crossing a
		 * frame boundary between register reads
		 */
		for (frame = 0; frame < PSR2_SU_STATUS_FRAMES; frame += 3) {
			val = intel_de_read(dev_priv,
					    PSR2_SU_STATUS(intel_dp->psr.transcoder, frame));
			su_frames_val[frame / 3] = val;
		}

		seq_puts(m, "Frame:\tPSR2 SU blocks:\n");

		for (frame = 0; frame < PSR2_SU_STATUS_FRAMES; frame++) {
			u32 su_blocks;

			su_blocks = su_frames_val[frame / 3] &
				    PSR2_SU_STATUS_MASK(frame);
			su_blocks = su_blocks >> PSR2_SU_STATUS_SHIFT(frame);
			seq_printf(m, "%d\t%d\n", frame, su_blocks);
		}

		seq_printf(m, "PSR2 selective fetch: %s\n",
			   enableddisabled(psr->psr2_sel_fetch_enabled));
	}

unlock:
	mutex_unlock(&psr->lock);
	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static int i915_edp_psr_status(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_dp *intel_dp = NULL;
	struct intel_encoder *encoder;

	if (!HAS_PSR(dev_priv))
		return -ENODEV;

	/* Find the first EDP which supports PSR */
	for_each_intel_encoder_with_psr(&dev_priv->drm, encoder) {
		intel_dp = enc_to_intel_dp(encoder);
		break;
	}

	if (!intel_dp)
		return -ENODEV;

	return intel_psr_status(m, intel_dp);
}

static int
i915_edp_psr_debug_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	struct intel_encoder *encoder;
	intel_wakeref_t wakeref;
	int ret = -ENODEV;

	if (!HAS_PSR(dev_priv))
		return ret;

	for_each_intel_encoder_with_psr(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		drm_dbg_kms(&dev_priv->drm, "Setting PSR debug to %llx\n", val);

		wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

		// TODO: split to each transcoder's PSR debug state
		ret = intel_psr_debug_set(intel_dp, val);

		intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
	}

	return ret;
}

static int
i915_edp_psr_debug_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;
	struct intel_encoder *encoder;

	if (!HAS_PSR(dev_priv))
		return -ENODEV;

	for_each_intel_encoder_with_psr(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		// TODO: split to each transcoder's PSR debug state
		*val = READ_ONCE(intel_dp->psr.debug);
		return 0;
	}

	return -ENODEV;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_edp_psr_debug_fops,
			i915_edp_psr_debug_get, i915_edp_psr_debug_set,
			"%llu\n");

static int i915_power_domain_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);

	intel_display_power_debug(i915, m);

	return 0;
}

static int i915_dmc_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	intel_wakeref_t wakeref;
	struct intel_dmc *dmc;
	i915_reg_t dc5_reg, dc6_reg = {};

	if (!HAS_DMC(dev_priv))
		return -ENODEV;

	dmc = &dev_priv->dmc;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	seq_printf(m, "fw loaded: %s\n", yesno(intel_dmc_has_payload(dev_priv)));
	seq_printf(m, "path: %s\n", dmc->fw_path);
	seq_printf(m, "Pipe A fw support: %s\n",
		   yesno(GRAPHICS_VER(dev_priv) >= 12));
	seq_printf(m, "Pipe A fw loaded: %s\n", yesno(dmc->dmc_info[DMC_FW_PIPEA].payload));
	seq_printf(m, "Pipe B fw support: %s\n", yesno(IS_ALDERLAKE_P(dev_priv)));
	seq_printf(m, "Pipe B fw loaded: %s\n", yesno(dmc->dmc_info[DMC_FW_PIPEB].payload));

	if (!intel_dmc_has_payload(dev_priv))
		goto out;

	seq_printf(m, "version: %d.%d\n", DMC_VERSION_MAJOR(dmc->version),
		   DMC_VERSION_MINOR(dmc->version));

	if (DISPLAY_VER(dev_priv) >= 12) {
		if (IS_DGFX(dev_priv)) {
			dc5_reg = DG1_DMC_DEBUG_DC5_COUNT;
		} else {
			dc5_reg = TGL_DMC_DEBUG_DC5_COUNT;
			dc6_reg = TGL_DMC_DEBUG_DC6_COUNT;
		}

		/*
		 * NOTE: DMC_DEBUG3 is a general purpose reg.
		 * According to B.Specs:49196 DMC f/w reuses DC5/6 counter
		 * reg for DC3CO debugging and validation,
		 * but TGL DMC f/w is using DMC_DEBUG3 reg for DC3CO counter.
		 */
		seq_printf(m, "DC3CO count: %d\n",
			   intel_de_read(dev_priv, DMC_DEBUG3));
	} else {
		dc5_reg = IS_BROXTON(dev_priv) ? BXT_DMC_DC3_DC5_COUNT :
						 SKL_DMC_DC3_DC5_COUNT;
		if (!IS_GEMINILAKE(dev_priv) && !IS_BROXTON(dev_priv))
			dc6_reg = SKL_DMC_DC5_DC6_COUNT;
	}

	seq_printf(m, "DC3 -> DC5 count: %d\n",
		   intel_de_read(dev_priv, dc5_reg));
	if (dc6_reg.reg)
		seq_printf(m, "DC5 -> DC6 count: %d\n",
			   intel_de_read(dev_priv, dc6_reg));

out:
	seq_printf(m, "program base: 0x%08x\n",
		   intel_de_read(dev_priv, DMC_PROGRAM(dmc->dmc_info[DMC_FW_MAIN].start_mmioaddr, 0)));
	seq_printf(m, "ssp base: 0x%08x\n",
		   intel_de_read(dev_priv, DMC_SSP_BASE));
	seq_printf(m, "htp: 0x%08x\n", intel_de_read(dev_priv, DMC_HTP_SKL));

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static void intel_seq_print_mode(struct seq_file *m, int tabs,
				 const struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < tabs; i++)
		seq_putc(m, '\t');

	seq_printf(m, DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));
}

static void intel_encoder_info(struct seq_file *m,
			       struct intel_crtc *crtc,
			       struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	seq_printf(m, "\t[ENCODER:%d:%s]: connectors:\n",
		   encoder->base.base.id, encoder->base.name);

	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		const struct drm_connector_state *conn_state =
			connector->state;

		if (conn_state->best_encoder != &encoder->base)
			continue;

		seq_printf(m, "\t\t[CONNECTOR:%d:%s]\n",
			   connector->base.id, connector->name);
	}
	drm_connector_list_iter_end(&conn_iter);
}

static void intel_panel_info(struct seq_file *m, struct intel_panel *panel)
{
	const struct drm_display_mode *mode = panel->fixed_mode;

	seq_printf(m, "\tfixed mode: " DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));
}

static void intel_hdcp_info(struct seq_file *m,
			    struct intel_connector *intel_connector)
{
	bool hdcp_cap, hdcp2_cap;

	if (!intel_connector->hdcp.shim) {
		seq_puts(m, "No Connector Support");
		goto out;
	}

	hdcp_cap = intel_hdcp_capable(intel_connector);
	hdcp2_cap = intel_hdcp2_capable(intel_connector);

	if (hdcp_cap)
		seq_puts(m, "HDCP1.4 ");
	if (hdcp2_cap)
		seq_puts(m, "HDCP2.2 ");

	if (!hdcp_cap && !hdcp2_cap)
		seq_puts(m, "None");

out:
	seq_puts(m, "\n");
}

static void intel_dp_info(struct seq_file *m,
			  struct intel_connector *intel_connector)
{
	struct intel_encoder *intel_encoder = intel_attached_encoder(intel_connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(intel_encoder);
	const struct drm_property_blob *edid = intel_connector->base.edid_blob_ptr;

	seq_printf(m, "\tDPCD rev: %x\n", intel_dp->dpcd[DP_DPCD_REV]);
	seq_printf(m, "\taudio support: %s\n", yesno(intel_dp->has_audio));
	if (intel_connector->base.connector_type == DRM_MODE_CONNECTOR_eDP)
		intel_panel_info(m, &intel_connector->panel);

	drm_dp_downstream_debug(m, intel_dp->dpcd, intel_dp->downstream_ports,
				edid ? edid->data : NULL, &intel_dp->aux);
}

static void intel_dp_mst_info(struct seq_file *m,
			      struct intel_connector *intel_connector)
{
	bool has_audio = intel_connector->port->has_audio;

	seq_printf(m, "\taudio support: %s\n", yesno(has_audio));
}

static void intel_hdmi_info(struct seq_file *m,
			    struct intel_connector *intel_connector)
{
	struct intel_encoder *intel_encoder = intel_attached_encoder(intel_connector);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(intel_encoder);

	seq_printf(m, "\taudio support: %s\n", yesno(intel_hdmi->has_audio));
}

static void intel_lvds_info(struct seq_file *m,
			    struct intel_connector *intel_connector)
{
	intel_panel_info(m, &intel_connector->panel);
}

static void intel_connector_info(struct seq_file *m,
				 struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	const struct drm_connector_state *conn_state = connector->state;
	struct intel_encoder *encoder =
		to_intel_encoder(conn_state->best_encoder);
	const struct drm_display_mode *mode;

	seq_printf(m, "[CONNECTOR:%d:%s]: status: %s\n",
		   connector->base.id, connector->name,
		   drm_get_connector_status_name(connector->status));

	if (connector->status == connector_status_disconnected)
		return;

	seq_printf(m, "\tphysical dimensions: %dx%dmm\n",
		   connector->display_info.width_mm,
		   connector->display_info.height_mm);
	seq_printf(m, "\tsubpixel order: %s\n",
		   drm_get_subpixel_order_name(connector->display_info.subpixel_order));
	seq_printf(m, "\tCEA rev: %d\n", connector->display_info.cea_rev);

	if (!encoder)
		return;

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		if (encoder->type == INTEL_OUTPUT_DP_MST)
			intel_dp_mst_info(m, intel_connector);
		else
			intel_dp_info(m, intel_connector);
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		if (encoder->type == INTEL_OUTPUT_LVDS)
			intel_lvds_info(m, intel_connector);
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
		if (encoder->type == INTEL_OUTPUT_HDMI ||
		    encoder->type == INTEL_OUTPUT_DDI)
			intel_hdmi_info(m, intel_connector);
		break;
	default:
		break;
	}

	seq_puts(m, "\tHDCP version: ");
	intel_hdcp_info(m, intel_connector);

	seq_printf(m, "\tmodes:\n");
	list_for_each_entry(mode, &connector->modes, head)
		intel_seq_print_mode(m, 2, mode);
}

static const char *plane_type(enum drm_plane_type type)
{
	switch (type) {
	case DRM_PLANE_TYPE_OVERLAY:
		return "OVL";
	case DRM_PLANE_TYPE_PRIMARY:
		return "PRI";
	case DRM_PLANE_TYPE_CURSOR:
		return "CUR";
	/*
	 * Deliberately omitting default: to generate compiler warnings
	 * when a new drm_plane_type gets added.
	 */
	}

	return "unknown";
}

static void plane_rotation(char *buf, size_t bufsize, unsigned int rotation)
{
	/*
	 * According to doc only one DRM_MODE_ROTATE_ is allowed but this
	 * will print them all to visualize if the values are misused
	 */
	snprintf(buf, bufsize,
		 "%s%s%s%s%s%s(0x%08x)",
		 (rotation & DRM_MODE_ROTATE_0) ? "0 " : "",
		 (rotation & DRM_MODE_ROTATE_90) ? "90 " : "",
		 (rotation & DRM_MODE_ROTATE_180) ? "180 " : "",
		 (rotation & DRM_MODE_ROTATE_270) ? "270 " : "",
		 (rotation & DRM_MODE_REFLECT_X) ? "FLIPX " : "",
		 (rotation & DRM_MODE_REFLECT_Y) ? "FLIPY " : "",
		 rotation);
}

static const char *plane_visibility(const struct intel_plane_state *plane_state)
{
	if (plane_state->uapi.visible)
		return "visible";

	if (plane_state->planar_slave)
		return "planar-slave";

	return "hidden";
}

static void intel_plane_uapi_info(struct seq_file *m, struct intel_plane *plane)
{
	const struct intel_plane_state *plane_state =
		to_intel_plane_state(plane->base.state);
	const struct drm_framebuffer *fb = plane_state->uapi.fb;
	struct drm_rect src, dst;
	char rot_str[48];

	src = drm_plane_state_src(&plane_state->uapi);
	dst = drm_plane_state_dest(&plane_state->uapi);

	plane_rotation(rot_str, sizeof(rot_str),
		       plane_state->uapi.rotation);

	seq_puts(m, "\t\tuapi: [FB:");
	if (fb)
		seq_printf(m, "%d] %p4cc,0x%llx,%dx%d", fb->base.id,
			   &fb->format->format, fb->modifier, fb->width,
			   fb->height);
	else
		seq_puts(m, "0] n/a,0x0,0x0,");
	seq_printf(m, ", visible=%s, src=" DRM_RECT_FP_FMT ", dst=" DRM_RECT_FMT
		   ", rotation=%s\n", plane_visibility(plane_state),
		   DRM_RECT_FP_ARG(&src), DRM_RECT_ARG(&dst), rot_str);

	if (plane_state->planar_linked_plane)
		seq_printf(m, "\t\tplanar: Linked to [PLANE:%d:%s] as a %s\n",
			   plane_state->planar_linked_plane->base.base.id, plane_state->planar_linked_plane->base.name,
			   plane_state->planar_slave ? "slave" : "master");
}

static void intel_plane_hw_info(struct seq_file *m, struct intel_plane *plane)
{
	const struct intel_plane_state *plane_state =
		to_intel_plane_state(plane->base.state);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	char rot_str[48];

	if (!fb)
		return;

	plane_rotation(rot_str, sizeof(rot_str),
		       plane_state->hw.rotation);

	seq_printf(m, "\t\thw: [FB:%d] %p4cc,0x%llx,%dx%d, visible=%s, src="
		   DRM_RECT_FP_FMT ", dst=" DRM_RECT_FMT ", rotation=%s\n",
		   fb->base.id, &fb->format->format,
		   fb->modifier, fb->width, fb->height,
		   yesno(plane_state->uapi.visible),
		   DRM_RECT_FP_ARG(&plane_state->uapi.src),
		   DRM_RECT_ARG(&plane_state->uapi.dst),
		   rot_str);
}

static void intel_plane_info(struct seq_file *m, struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_plane *plane;

	for_each_intel_plane_on_crtc(&dev_priv->drm, crtc, plane) {
		seq_printf(m, "\t[PLANE:%d:%s]: type=%s\n",
			   plane->base.base.id, plane->base.name,
			   plane_type(plane->base.type));
		intel_plane_uapi_info(m, plane);
		intel_plane_hw_info(m, plane);
	}
}

static void intel_scaler_info(struct seq_file *m, struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	int num_scalers = crtc->num_scalers;
	int i;

	/* Not all platformas have a scaler */
	if (num_scalers) {
		seq_printf(m, "\tnum_scalers=%d, scaler_users=%x scaler_id=%d",
			   num_scalers,
			   crtc_state->scaler_state.scaler_users,
			   crtc_state->scaler_state.scaler_id);

		for (i = 0; i < num_scalers; i++) {
			const struct intel_scaler *sc =
				&crtc_state->scaler_state.scalers[i];

			seq_printf(m, ", scalers[%d]: use=%s, mode=%x",
				   i, yesno(sc->in_use), sc->mode);
		}
		seq_puts(m, "\n");
	} else {
		seq_puts(m, "\tNo scalers available on this platform\n");
	}
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_VBLANK_EVADE)
static void crtc_updates_info(struct seq_file *m,
			      struct intel_crtc *crtc,
			      const char *hdr)
{
	u64 count;
	int row;

	count = 0;
	for (row = 0; row < ARRAY_SIZE(crtc->debug.vbl.times); row++)
		count += crtc->debug.vbl.times[row];
	seq_printf(m, "%sUpdates: %llu\n", hdr, count);
	if (!count)
		return;

	for (row = 0; row < ARRAY_SIZE(crtc->debug.vbl.times); row++) {
		char columns[80] = "       |";
		unsigned int x;

		if (row & 1) {
			const char *units;

			if (row > 10) {
				x = 1000000;
				units = "ms";
			} else {
				x = 1000;
				units = "us";
			}

			snprintf(columns, sizeof(columns), "%4ld%s |",
				 DIV_ROUND_CLOSEST(BIT(row + 9), x), units);
		}

		if (crtc->debug.vbl.times[row]) {
			x = ilog2(crtc->debug.vbl.times[row]);
			memset(columns + 8, '*', x);
			columns[8 + x] = '\0';
		}

		seq_printf(m, "%s%s\n", hdr, columns);
	}

	seq_printf(m, "%sMin update: %lluns\n",
		   hdr, crtc->debug.vbl.min);
	seq_printf(m, "%sMax update: %lluns\n",
		   hdr, crtc->debug.vbl.max);
	seq_printf(m, "%sAverage update: %lluns\n",
		   hdr, div64_u64(crtc->debug.vbl.sum,  count));
	seq_printf(m, "%sOverruns > %uus: %u\n",
		   hdr, VBLANK_EVASION_TIME_US, crtc->debug.vbl.over);
}

static int crtc_updates_show(struct seq_file *m, void *data)
{
	crtc_updates_info(m, m->private, "");
	return 0;
}

static int crtc_updates_open(struct inode *inode, struct file *file)
{
	return single_open(file, crtc_updates_show, inode->i_private);
}

static ssize_t crtc_updates_write(struct file *file,
				  const char __user *ubuf,
				  size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_crtc *crtc = m->private;

	/* May race with an update. Meh. */
	memset(&crtc->debug.vbl, 0, sizeof(crtc->debug.vbl));

	return len;
}

static const struct file_operations crtc_updates_fops = {
	.owner = THIS_MODULE,
	.open = crtc_updates_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = crtc_updates_write
};

static void crtc_updates_add(struct drm_crtc *crtc)
{
	debugfs_create_file("i915_update_info", 0644, crtc->debugfs_entry,
			    to_intel_crtc(crtc), &crtc_updates_fops);
}

#else
static void crtc_updates_info(struct seq_file *m,
			      struct intel_crtc *crtc,
			      const char *hdr)
{
}

static void crtc_updates_add(struct drm_crtc *crtc)
{
}
#endif

static void intel_crtc_info(struct seq_file *m, struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	const struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct intel_encoder *encoder;

	seq_printf(m, "[CRTC:%d:%s]:\n",
		   crtc->base.base.id, crtc->base.name);

	seq_printf(m, "\tuapi: enable=%s, active=%s, mode=" DRM_MODE_FMT "\n",
		   yesno(crtc_state->uapi.enable),
		   yesno(crtc_state->uapi.active),
		   DRM_MODE_ARG(&crtc_state->uapi.mode));

	if (crtc_state->hw.enable) {
		seq_printf(m, "\thw: active=%s, adjusted_mode=" DRM_MODE_FMT "\n",
			   yesno(crtc_state->hw.active),
			   DRM_MODE_ARG(&crtc_state->hw.adjusted_mode));

		seq_printf(m, "\tpipe src size=%dx%d, dither=%s, bpp=%d\n",
			   crtc_state->pipe_src_w, crtc_state->pipe_src_h,
			   yesno(crtc_state->dither), crtc_state->pipe_bpp);

		intel_scaler_info(m, crtc);
	}

	if (crtc_state->bigjoiner)
		seq_printf(m, "\tLinked to [CRTC:%d:%s] as a %s\n",
			   crtc_state->bigjoiner_linked_crtc->base.base.id,
			   crtc_state->bigjoiner_linked_crtc->base.name,
			   crtc_state->bigjoiner_slave ? "slave" : "master");

	for_each_intel_encoder_mask(&dev_priv->drm, encoder,
				    crtc_state->uapi.encoder_mask)
		intel_encoder_info(m, crtc, encoder);

	intel_plane_info(m, crtc);

	seq_printf(m, "\tunderrun reporting: cpu=%s pch=%s\n",
		   yesno(!crtc->cpu_fifo_underrun_disabled),
		   yesno(!crtc->pch_fifo_underrun_disabled));

	crtc_updates_info(m, crtc, "\t");
}

static int i915_display_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	drm_modeset_lock_all(dev);

	seq_printf(m, "CRTC info\n");
	seq_printf(m, "---------\n");
	for_each_intel_crtc(dev, crtc)
		intel_crtc_info(m, crtc);

	seq_printf(m, "\n");
	seq_printf(m, "Connector info\n");
	seq_printf(m, "--------------\n");
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		intel_connector_info(m, connector);
	drm_connector_list_iter_end(&conn_iter);

	drm_modeset_unlock_all(dev);

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static int i915_shared_dplls_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	int i;

	drm_modeset_lock_all(dev);

	seq_printf(m, "PLL refclks: non-SSC: %d kHz, SSC: %d kHz\n",
		   dev_priv->dpll.ref_clks.nssc,
		   dev_priv->dpll.ref_clks.ssc);

	for (i = 0; i < dev_priv->dpll.num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->dpll.shared_dplls[i];

		seq_printf(m, "DPLL%i: %s, id: %i\n", i, pll->info->name,
			   pll->info->id);
		seq_printf(m, " pipe_mask: 0x%x, active: 0x%x, on: %s\n",
			   pll->state.pipe_mask, pll->active_mask, yesno(pll->on));
		seq_printf(m, " tracked hardware state:\n");
		seq_printf(m, " dpll:    0x%08x\n", pll->state.hw_state.dpll);
		seq_printf(m, " dpll_md: 0x%08x\n",
			   pll->state.hw_state.dpll_md);
		seq_printf(m, " fp0:     0x%08x\n", pll->state.hw_state.fp0);
		seq_printf(m, " fp1:     0x%08x\n", pll->state.hw_state.fp1);
		seq_printf(m, " wrpll:   0x%08x\n", pll->state.hw_state.wrpll);
		seq_printf(m, " cfgcr0:  0x%08x\n", pll->state.hw_state.cfgcr0);
		seq_printf(m, " cfgcr1:  0x%08x\n", pll->state.hw_state.cfgcr1);
		seq_printf(m, " mg_refclkin_ctl:        0x%08x\n",
			   pll->state.hw_state.mg_refclkin_ctl);
		seq_printf(m, " mg_clktop2_coreclkctl1: 0x%08x\n",
			   pll->state.hw_state.mg_clktop2_coreclkctl1);
		seq_printf(m, " mg_clktop2_hsclkctl:    0x%08x\n",
			   pll->state.hw_state.mg_clktop2_hsclkctl);
		seq_printf(m, " mg_pll_div0:  0x%08x\n",
			   pll->state.hw_state.mg_pll_div0);
		seq_printf(m, " mg_pll_div1:  0x%08x\n",
			   pll->state.hw_state.mg_pll_div1);
		seq_printf(m, " mg_pll_lf:    0x%08x\n",
			   pll->state.hw_state.mg_pll_lf);
		seq_printf(m, " mg_pll_frac_lock: 0x%08x\n",
			   pll->state.hw_state.mg_pll_frac_lock);
		seq_printf(m, " mg_pll_ssc:   0x%08x\n",
			   pll->state.hw_state.mg_pll_ssc);
		seq_printf(m, " mg_pll_bias:  0x%08x\n",
			   pll->state.hw_state.mg_pll_bias);
		seq_printf(m, " mg_pll_tdc_coldst_bias: 0x%08x\n",
			   pll->state.hw_state.mg_pll_tdc_coldst_bias);
	}
	drm_modeset_unlock_all(dev);

	return 0;
}

static int i915_ipc_status_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;

	seq_printf(m, "Isochronous Priority Control: %s\n",
			yesno(dev_priv->ipc_enabled));
	return 0;
}

static int i915_ipc_status_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (!HAS_IPC(dev_priv))
		return -ENODEV;

	return single_open(file, i915_ipc_status_show, dev_priv);
}

static ssize_t i915_ipc_status_write(struct file *file, const char __user *ubuf,
				     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	intel_wakeref_t wakeref;
	bool enable;
	int ret;

	ret = kstrtobool_from_user(ubuf, len, &enable);
	if (ret < 0)
		return ret;

	with_intel_runtime_pm(&dev_priv->runtime_pm, wakeref) {
		if (!dev_priv->ipc_enabled && enable)
			drm_info(&dev_priv->drm,
				 "Enabling IPC: WM will be proper only after next commit\n");
		dev_priv->ipc_enabled = enable;
		intel_enable_ipc(dev_priv);
	}

	return len;
}

static const struct file_operations i915_ipc_status_fops = {
	.owner = THIS_MODULE,
	.open = i915_ipc_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_ipc_status_write
};

static int i915_ddb_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct skl_ddb_entry *entry;
	struct intel_crtc *crtc;

	if (DISPLAY_VER(dev_priv) < 9)
		return -ENODEV;

	drm_modeset_lock_all(dev);

	seq_printf(m, "%-15s%8s%8s%8s\n", "", "Start", "End", "Size");

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		enum pipe pipe = crtc->pipe;
		enum plane_id plane_id;

		seq_printf(m, "Pipe %c\n", pipe_name(pipe));

		for_each_plane_id_on_crtc(crtc, plane_id) {
			entry = &crtc_state->wm.skl.plane_ddb_y[plane_id];
			seq_printf(m, "  Plane%-8d%8u%8u%8u\n", plane_id + 1,
				   entry->start, entry->end,
				   skl_ddb_entry_size(entry));
		}

		entry = &crtc_state->wm.skl.plane_ddb_y[PLANE_CURSOR];
		seq_printf(m, "  %-13s%8u%8u%8u\n", "Cursor", entry->start,
			   entry->end, skl_ddb_entry_size(entry));
	}

	drm_modeset_unlock_all(dev);

	return 0;
}

static void drrs_status_per_crtc(struct seq_file *m,
				 struct drm_device *dev,
				 struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_drrs *drrs = &dev_priv->drrs;
	int vrefresh = 0;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		bool supported = false;

		if (connector->state->crtc != &crtc->base)
			continue;

		seq_printf(m, "%s:\n", connector->name);

		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP &&
		    drrs->type == SEAMLESS_DRRS_SUPPORT)
			supported = true;

		seq_printf(m, "\tDRRS Supported: %s\n", yesno(supported));
	}
	drm_connector_list_iter_end(&conn_iter);

	seq_puts(m, "\n");

	if (to_intel_crtc_state(crtc->base.state)->has_drrs) {
		struct intel_panel *panel;

		mutex_lock(&drrs->mutex);
		/* DRRS Supported */
		seq_puts(m, "\tDRRS Enabled: Yes\n");

		/* disable_drrs() will make drrs->dp NULL */
		if (!drrs->dp) {
			seq_puts(m, "Idleness DRRS: Disabled\n");
			mutex_unlock(&drrs->mutex);
			return;
		}

		panel = &drrs->dp->attached_connector->panel;
		seq_printf(m, "\t\tBusy_frontbuffer_bits: 0x%X",
					drrs->busy_frontbuffer_bits);

		seq_puts(m, "\n\t\t");
		if (drrs->refresh_rate_type == DRRS_HIGH_RR) {
			seq_puts(m, "DRRS_State: DRRS_HIGH_RR\n");
			vrefresh = drm_mode_vrefresh(panel->fixed_mode);
		} else if (drrs->refresh_rate_type == DRRS_LOW_RR) {
			seq_puts(m, "DRRS_State: DRRS_LOW_RR\n");
			vrefresh = drm_mode_vrefresh(panel->downclock_mode);
		} else {
			seq_printf(m, "DRRS_State: Unknown(%d)\n",
						drrs->refresh_rate_type);
			mutex_unlock(&drrs->mutex);
			return;
		}
		seq_printf(m, "\t\tVrefresh: %d", vrefresh);

		seq_puts(m, "\n\t\t");
		mutex_unlock(&drrs->mutex);
	} else {
		/* DRRS not supported. Print the VBT parameter*/
		seq_puts(m, "\tDRRS Enabled : No");
	}
	seq_puts(m, "\n");
}

static int i915_drrs_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;
	int active_crtc_cnt = 0;

	drm_modeset_lock_all(dev);
	for_each_intel_crtc(dev, crtc) {
		if (crtc->base.state->active) {
			active_crtc_cnt++;
			seq_printf(m, "\nCRTC %d:  ", active_crtc_cnt);

			drrs_status_per_crtc(m, dev, crtc);
		}
	}
	drm_modeset_unlock_all(dev);

	if (!active_crtc_cnt)
		seq_puts(m, "No active crtc found\n");

	return 0;
}

static bool
intel_lpsp_power_well_enabled(struct drm_i915_private *i915,
			      enum i915_power_well_id power_well_id)
{
	intel_wakeref_t wakeref;
	bool is_enabled;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	is_enabled = intel_display_power_well_is_enabled(i915,
							 power_well_id);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return is_enabled;
}

static int i915_lpsp_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	bool lpsp_enabled = false;

	if (DISPLAY_VER(i915) >= 13 || IS_DISPLAY_VER(i915, 9, 10)) {
		lpsp_enabled = !intel_lpsp_power_well_enabled(i915, SKL_DISP_PW_2);
	} else if (IS_DISPLAY_VER(i915, 11, 12)) {
		lpsp_enabled = !intel_lpsp_power_well_enabled(i915, ICL_DISP_PW_3);
	} else if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
		lpsp_enabled = !intel_lpsp_power_well_enabled(i915, HSW_DISP_PW_GLOBAL);
	} else {
		seq_puts(m, "LPSP: not supported\n");
		return 0;
	}

	seq_printf(m, "LPSP: %s\n", enableddisabled(lpsp_enabled));

	return 0;
}

static int i915_dp_mst_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *intel_encoder;
	struct intel_digital_port *dig_port;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		intel_encoder = intel_attached_encoder(to_intel_connector(connector));
		if (!intel_encoder || intel_encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		dig_port = enc_to_dig_port(intel_encoder);
		if (!intel_dp_mst_source_support(&dig_port->dp))
			continue;

		seq_printf(m, "MST Source Port [ENCODER:%d:%s]\n",
			   dig_port->base.base.base.id,
			   dig_port->base.base.name);
		drm_dp_mst_dump_topology(m, &dig_port->dp.mst_mgr);
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static ssize_t i915_displayport_test_active_write(struct file *file,
						  const char __user *ubuf,
						  size_t len, loff_t *offp)
{
	char *input_buffer;
	int status = 0;
	struct drm_device *dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;
	int val = 0;

	dev = ((struct seq_file *)file->private_data)->private;

	if (len == 0)
		return 0;

	input_buffer = memdup_user_nul(ubuf, len);
	if (IS_ERR(input_buffer))
		return PTR_ERR(input_buffer);

	drm_dbg(&to_i915(dev)->drm,
		"Copied %d bytes from user\n", (unsigned int)len);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			status = kstrtoint(input_buffer, 10, &val);
			if (status < 0)
				break;
			drm_dbg(&to_i915(dev)->drm,
				"Got %d for test active\n", val);
			/* To prevent erroneous activation of the compliance
			 * testing code, only accept an actual value of 1 here
			 */
			if (val == 1)
				intel_dp->compliance.test_active = true;
			else
				intel_dp->compliance.test_active = false;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	kfree(input_buffer);
	if (status < 0)
		return status;

	*offp += len;
	return len;
}

static int i915_displayport_test_active_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			if (intel_dp->compliance.test_active)
				seq_puts(m, "1");
			else
				seq_puts(m, "0");
		} else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static int i915_displayport_test_active_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, i915_displayport_test_active_show,
			   inode->i_private);
}

static const struct file_operations i915_displayport_test_active_fops = {
	.owner = THIS_MODULE,
	.open = i915_displayport_test_active_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_displayport_test_active_write
};

static int i915_displayport_test_data_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			if (intel_dp->compliance.test_type ==
			    DP_TEST_LINK_EDID_READ)
				seq_printf(m, "%lx",
					   intel_dp->compliance.test_data.edid);
			else if (intel_dp->compliance.test_type ==
				 DP_TEST_LINK_VIDEO_PATTERN) {
				seq_printf(m, "hdisplay: %d\n",
					   intel_dp->compliance.test_data.hdisplay);
				seq_printf(m, "vdisplay: %d\n",
					   intel_dp->compliance.test_data.vdisplay);
				seq_printf(m, "bpc: %u\n",
					   intel_dp->compliance.test_data.bpc);
			} else if (intel_dp->compliance.test_type ==
				   DP_TEST_LINK_PHY_TEST_PATTERN) {
				seq_printf(m, "pattern: %d\n",
					   intel_dp->compliance.test_data.phytest.phy_pattern);
				seq_printf(m, "Number of lanes: %d\n",
					   intel_dp->compliance.test_data.phytest.num_lanes);
				seq_printf(m, "Link Rate: %d\n",
					   intel_dp->compliance.test_data.phytest.link_rate);
				seq_printf(m, "level: %02x\n",
					   intel_dp->train_set[0]);
			}
		} else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_displayport_test_data);

static int i915_displayport_test_type_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			seq_printf(m, "%02lx\n", intel_dp->compliance.test_type);
		} else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_displayport_test_type);

static void wm_latency_show(struct seq_file *m, const u16 wm[8])
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	int level;
	int num_levels;

	if (IS_CHERRYVIEW(dev_priv))
		num_levels = 3;
	else if (IS_VALLEYVIEW(dev_priv))
		num_levels = 1;
	else if (IS_G4X(dev_priv))
		num_levels = 3;
	else
		num_levels = ilk_wm_max_level(dev_priv) + 1;

	drm_modeset_lock_all(dev);

	for (level = 0; level < num_levels; level++) {
		unsigned int latency = wm[level];

		/*
		 * - WM1+ latency values in 0.5us units
		 * - latencies are in us on gen9/vlv/chv
		 */
		if (DISPLAY_VER(dev_priv) >= 9 ||
		    IS_VALLEYVIEW(dev_priv) ||
		    IS_CHERRYVIEW(dev_priv) ||
		    IS_G4X(dev_priv))
			latency *= 10;
		else if (level > 0)
			latency *= 5;

		seq_printf(m, "WM%d %u (%u.%u usec)\n",
			   level, wm[level], latency / 10, latency % 10);
	}

	drm_modeset_unlock_all(dev);
}

static int pri_wm_latency_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	const u16 *latencies;

	if (DISPLAY_VER(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.pri_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int spr_wm_latency_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	const u16 *latencies;

	if (DISPLAY_VER(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.spr_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int cur_wm_latency_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	const u16 *latencies;

	if (DISPLAY_VER(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.cur_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int pri_wm_latency_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (DISPLAY_VER(dev_priv) < 5 && !IS_G4X(dev_priv))
		return -ENODEV;

	return single_open(file, pri_wm_latency_show, dev_priv);
}

static int spr_wm_latency_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (HAS_GMCH(dev_priv))
		return -ENODEV;

	return single_open(file, spr_wm_latency_show, dev_priv);
}

static int cur_wm_latency_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (HAS_GMCH(dev_priv))
		return -ENODEV;

	return single_open(file, cur_wm_latency_show, dev_priv);
}

static ssize_t wm_latency_write(struct file *file, const char __user *ubuf,
				size_t len, loff_t *offp, u16 wm[8])
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	u16 new[8] = { 0 };
	int num_levels;
	int level;
	int ret;
	char tmp[32];

	if (IS_CHERRYVIEW(dev_priv))
		num_levels = 3;
	else if (IS_VALLEYVIEW(dev_priv))
		num_levels = 1;
	else if (IS_G4X(dev_priv))
		num_levels = 3;
	else
		num_levels = ilk_wm_max_level(dev_priv) + 1;

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	ret = sscanf(tmp, "%hu %hu %hu %hu %hu %hu %hu %hu",
		     &new[0], &new[1], &new[2], &new[3],
		     &new[4], &new[5], &new[6], &new[7]);
	if (ret != num_levels)
		return -EINVAL;

	drm_modeset_lock_all(dev);

	for (level = 0; level < num_levels; level++)
		wm[level] = new[level];

	drm_modeset_unlock_all(dev);

	return len;
}


static ssize_t pri_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	u16 *latencies;

	if (DISPLAY_VER(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.pri_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static ssize_t spr_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	u16 *latencies;

	if (DISPLAY_VER(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.spr_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static ssize_t cur_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	u16 *latencies;

	if (DISPLAY_VER(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.cur_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static const struct file_operations i915_pri_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = pri_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = pri_wm_latency_write
};

static const struct file_operations i915_spr_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = spr_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = spr_wm_latency_write
};

static const struct file_operations i915_cur_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = cur_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = cur_wm_latency_write
};

static int i915_hpd_storm_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct i915_hotplug *hotplug = &dev_priv->hotplug;

	/* Synchronize with everything first in case there's been an HPD
	 * storm, but we haven't finished handling it in the kernel yet
	 */
	intel_synchronize_irq(dev_priv);
	flush_work(&dev_priv->hotplug.dig_port_work);
	flush_delayed_work(&dev_priv->hotplug.hotplug_work);

	seq_printf(m, "Threshold: %d\n", hotplug->hpd_storm_threshold);
	seq_printf(m, "Detected: %s\n",
		   yesno(delayed_work_pending(&hotplug->reenable_work)));

	return 0;
}

static ssize_t i915_hpd_storm_ctl_write(struct file *file,
					const char __user *ubuf, size_t len,
					loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct i915_hotplug *hotplug = &dev_priv->hotplug;
	unsigned int new_threshold;
	int i;
	char *newline;
	char tmp[16];

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	/* Strip newline, if any */
	newline = strchr(tmp, '\n');
	if (newline)
		*newline = '\0';

	if (strcmp(tmp, "reset") == 0)
		new_threshold = HPD_STORM_DEFAULT_THRESHOLD;
	else if (kstrtouint(tmp, 10, &new_threshold) != 0)
		return -EINVAL;

	if (new_threshold > 0)
		drm_dbg_kms(&dev_priv->drm,
			    "Setting HPD storm detection threshold to %d\n",
			    new_threshold);
	else
		drm_dbg_kms(&dev_priv->drm, "Disabling HPD storm detection\n");

	spin_lock_irq(&dev_priv->irq_lock);
	hotplug->hpd_storm_threshold = new_threshold;
	/* Reset the HPD storm stats so we don't accidentally trigger a storm */
	for_each_hpd_pin(i)
		hotplug->stats[i].count = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Re-enable hpd immediately if we were in an irq storm */
	flush_delayed_work(&dev_priv->hotplug.reenable_work);

	return len;
}

static int i915_hpd_storm_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_hpd_storm_ctl_show, inode->i_private);
}

static const struct file_operations i915_hpd_storm_ctl_fops = {
	.owner = THIS_MODULE,
	.open = i915_hpd_storm_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_hpd_storm_ctl_write
};

static int i915_hpd_short_storm_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;

	seq_printf(m, "Enabled: %s\n",
		   yesno(dev_priv->hotplug.hpd_short_storm_enabled));

	return 0;
}

static int
i915_hpd_short_storm_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_hpd_short_storm_ctl_show,
			   inode->i_private);
}

static ssize_t i915_hpd_short_storm_ctl_write(struct file *file,
					      const char __user *ubuf,
					      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct i915_hotplug *hotplug = &dev_priv->hotplug;
	char *newline;
	char tmp[16];
	int i;
	bool new_state;

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	/* Strip newline, if any */
	newline = strchr(tmp, '\n');
	if (newline)
		*newline = '\0';

	/* Reset to the "default" state for this system */
	if (strcmp(tmp, "reset") == 0)
		new_state = !HAS_DP_MST(dev_priv);
	else if (kstrtobool(tmp, &new_state) != 0)
		return -EINVAL;

	drm_dbg_kms(&dev_priv->drm, "%sabling HPD short storm detection\n",
		    new_state ? "En" : "Dis");

	spin_lock_irq(&dev_priv->irq_lock);
	hotplug->hpd_short_storm_enabled = new_state;
	/* Reset the HPD storm stats so we don't accidentally trigger a storm */
	for_each_hpd_pin(i)
		hotplug->stats[i].count = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Re-enable hpd immediately if we were in an irq storm */
	flush_delayed_work(&dev_priv->hotplug.reenable_work);

	return len;
}

static const struct file_operations i915_hpd_short_storm_ctl_fops = {
	.owner = THIS_MODULE,
	.open = i915_hpd_short_storm_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_hpd_short_storm_ctl_write,
};

static int i915_drrs_ctl_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;

	if (DISPLAY_VER(dev_priv) < 7)
		return -ENODEV;

	for_each_intel_crtc(dev, crtc) {
		struct drm_connector_list_iter conn_iter;
		struct intel_crtc_state *crtc_state;
		struct drm_connector *connector;
		struct drm_crtc_commit *commit;
		int ret;

		ret = drm_modeset_lock_single_interruptible(&crtc->base.mutex);
		if (ret)
			return ret;

		crtc_state = to_intel_crtc_state(crtc->base.state);

		if (!crtc_state->hw.active ||
		    !crtc_state->has_drrs)
			goto out;

		commit = crtc_state->uapi.commit;
		if (commit) {
			ret = wait_for_completion_interruptible(&commit->hw_done);
			if (ret)
				goto out;
		}

		drm_connector_list_iter_begin(dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter) {
			struct intel_encoder *encoder;
			struct intel_dp *intel_dp;

			if (!(crtc_state->uapi.connector_mask &
			      drm_connector_mask(connector)))
				continue;

			encoder = intel_attached_encoder(to_intel_connector(connector));
			if (encoder->type != INTEL_OUTPUT_EDP)
				continue;

			drm_dbg(&dev_priv->drm,
				"Manually %sabling DRRS. %llu\n",
				val ? "en" : "dis", val);

			intel_dp = enc_to_intel_dp(encoder);
			if (val)
				intel_drrs_enable(intel_dp, crtc_state);
			else
				intel_drrs_disable(intel_dp, crtc_state);
		}
		drm_connector_list_iter_end(&conn_iter);

out:
		drm_modeset_unlock(&crtc->base.mutex);
		if (ret)
			return ret;
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_drrs_ctl_fops, NULL, i915_drrs_ctl_set, "%llu\n");

static ssize_t
i915_fifo_underrun_reset_write(struct file *filp,
			       const char __user *ubuf,
			       size_t cnt, loff_t *ppos)
{
	struct drm_i915_private *dev_priv = filp->private_data;
	struct intel_crtc *crtc;
	struct drm_device *dev = &dev_priv->drm;
	int ret;
	bool reset;

	ret = kstrtobool_from_user(ubuf, cnt, &reset);
	if (ret)
		return ret;

	if (!reset)
		return cnt;

	for_each_intel_crtc(dev, crtc) {
		struct drm_crtc_commit *commit;
		struct intel_crtc_state *crtc_state;

		ret = drm_modeset_lock_single_interruptible(&crtc->base.mutex);
		if (ret)
			return ret;

		crtc_state = to_intel_crtc_state(crtc->base.state);
		commit = crtc_state->uapi.commit;
		if (commit) {
			ret = wait_for_completion_interruptible(&commit->hw_done);
			if (!ret)
				ret = wait_for_completion_interruptible(&commit->flip_done);
		}

		if (!ret && crtc_state->hw.active) {
			drm_dbg_kms(&dev_priv->drm,
				    "Re-arming FIFO underruns on pipe %c\n",
				    pipe_name(crtc->pipe));

			intel_crtc_arm_fifo_underrun(crtc, crtc_state);
		}

		drm_modeset_unlock(&crtc->base.mutex);

		if (ret)
			return ret;
	}

	intel_fbc_reset_underrun(dev_priv);

	return cnt;
}

static const struct file_operations i915_fifo_underrun_reset_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = i915_fifo_underrun_reset_write,
	.llseek = default_llseek,
};

static const struct drm_info_list intel_display_debugfs_list[] = {
	{"i915_frontbuffer_tracking", i915_frontbuffer_tracking, 0},
	{"i915_ips_status", i915_ips_status, 0},
	{"i915_sr_status", i915_sr_status, 0},
	{"i915_opregion", i915_opregion, 0},
	{"i915_vbt", i915_vbt, 0},
	{"i915_gem_framebuffer", i915_gem_framebuffer_info, 0},
	{"i915_edp_psr_status", i915_edp_psr_status, 0},
	{"i915_power_domain_info", i915_power_domain_info, 0},
	{"i915_dmc_info", i915_dmc_info, 0},
	{"i915_display_info", i915_display_info, 0},
	{"i915_shared_dplls_info", i915_shared_dplls_info, 0},
	{"i915_dp_mst_info", i915_dp_mst_info, 0},
	{"i915_ddb_info", i915_ddb_info, 0},
	{"i915_drrs_status", i915_drrs_status, 0},
	{"i915_lpsp_status", i915_lpsp_status, 0},
};

static const struct {
	const char *name;
	const struct file_operations *fops;
} intel_display_debugfs_files[] = {
	{"i915_fifo_underrun_reset", &i915_fifo_underrun_reset_ops},
	{"i915_pri_wm_latency", &i915_pri_wm_latency_fops},
	{"i915_spr_wm_latency", &i915_spr_wm_latency_fops},
	{"i915_cur_wm_latency", &i915_cur_wm_latency_fops},
	{"i915_dp_test_data", &i915_displayport_test_data_fops},
	{"i915_dp_test_type", &i915_displayport_test_type_fops},
	{"i915_dp_test_active", &i915_displayport_test_active_fops},
	{"i915_hpd_storm_ctl", &i915_hpd_storm_ctl_fops},
	{"i915_hpd_short_storm_ctl", &i915_hpd_short_storm_ctl_fops},
	{"i915_ipc_status", &i915_ipc_status_fops},
	{"i915_drrs_ctl", &i915_drrs_ctl_fops},
	{"i915_edp_psr_debug", &i915_edp_psr_debug_fops},
};

void intel_display_debugfs_register(struct drm_i915_private *i915)
{
	struct drm_minor *minor = i915->drm.primary;
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_display_debugfs_files); i++) {
		debugfs_create_file(intel_display_debugfs_files[i].name,
				    S_IRUGO | S_IWUSR,
				    minor->debugfs_root,
				    to_i915(minor->dev),
				    intel_display_debugfs_files[i].fops);
	}

	drm_debugfs_create_files(intel_display_debugfs_list,
				 ARRAY_SIZE(intel_display_debugfs_list),
				 minor->debugfs_root, minor);

	intel_fbc_debugfs_register(i915);
}

static int i915_panel_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct intel_dp *intel_dp =
		intel_attached_dp(to_intel_connector(connector));

	if (connector->status != connector_status_connected)
		return -ENODEV;

	seq_printf(m, "Panel power up delay: %d\n",
		   intel_dp->pps.panel_power_up_delay);
	seq_printf(m, "Panel power down delay: %d\n",
		   intel_dp->pps.panel_power_down_delay);
	seq_printf(m, "Backlight on delay: %d\n",
		   intel_dp->pps.backlight_on_delay);
	seq_printf(m, "Backlight off delay: %d\n",
		   intel_dp->pps.backlight_off_delay);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_panel);

static int i915_hdcp_sink_capability_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	int ret;

	ret = drm_modeset_lock_single_interruptible(&i915->drm.mode_config.connection_mutex);
	if (ret)
		return ret;

	if (!connector->encoder || connector->status != connector_status_connected) {
		ret = -ENODEV;
		goto out;
	}

	seq_printf(m, "%s:%d HDCP version: ", connector->name,
		   connector->base.id);
	intel_hdcp_info(m, intel_connector);

out:
	drm_modeset_unlock(&i915->drm.mode_config.connection_mutex);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(i915_hdcp_sink_capability);

static int i915_psr_status_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct intel_dp *intel_dp =
		intel_attached_dp(to_intel_connector(connector));

	return intel_psr_status(m, intel_dp);
}
DEFINE_SHOW_ATTRIBUTE(i915_psr_status);

static int i915_lpsp_capability_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_encoder *encoder;
	bool lpsp_capable = false;

	encoder = intel_attached_encoder(to_intel_connector(connector));
	if (!encoder)
		return -ENODEV;

	if (connector->status != connector_status_connected)
		return -ENODEV;

	if (DISPLAY_VER(i915) >= 13)
		lpsp_capable = encoder->port <= PORT_B;
	else if (DISPLAY_VER(i915) >= 12)
		/*
		 * Actually TGL can drive LPSP on port till DDI_C
		 * but there is no physical connected DDI_C on TGL sku's,
		 * even driver is not initilizing DDI_C port for gen12.
		 */
		lpsp_capable = encoder->port <= PORT_B;
	else if (DISPLAY_VER(i915) == 11)
		lpsp_capable = (connector->connector_type == DRM_MODE_CONNECTOR_DSI ||
				connector->connector_type == DRM_MODE_CONNECTOR_eDP);
	else if (IS_DISPLAY_VER(i915, 9, 10))
		lpsp_capable = (encoder->port == PORT_A &&
				(connector->connector_type == DRM_MODE_CONNECTOR_DSI ||
				 connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
				 connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort));
	else if (IS_HASWELL(i915) || IS_BROADWELL(i915))
		lpsp_capable = connector->connector_type == DRM_MODE_CONNECTOR_eDP;

	seq_printf(m, "LPSP: %s\n", lpsp_capable ? "capable" : "incapable");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_lpsp_capability);

static int i915_dsc_fec_support_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc;
	struct intel_dp *intel_dp;
	struct drm_modeset_acquire_ctx ctx;
	struct intel_crtc_state *crtc_state = NULL;
	int ret = 0;
	bool try_again = false;

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);

	do {
		try_again = false;
		ret = drm_modeset_lock(&dev->mode_config.connection_mutex,
				       &ctx);
		if (ret) {
			if (ret == -EDEADLK && !drm_modeset_backoff(&ctx)) {
				try_again = true;
				continue;
			}
			break;
		}
		crtc = connector->state->crtc;
		if (connector->status != connector_status_connected || !crtc) {
			ret = -ENODEV;
			break;
		}
		ret = drm_modeset_lock(&crtc->mutex, &ctx);
		if (ret == -EDEADLK) {
			ret = drm_modeset_backoff(&ctx);
			if (!ret) {
				try_again = true;
				continue;
			}
			break;
		} else if (ret) {
			break;
		}
		intel_dp = intel_attached_dp(to_intel_connector(connector));
		crtc_state = to_intel_crtc_state(crtc->state);
		seq_printf(m, "DSC_Enabled: %s\n",
			   yesno(crtc_state->dsc.compression_enable));
		seq_printf(m, "DSC_Sink_Support: %s\n",
			   yesno(drm_dp_sink_supports_dsc(intel_dp->dsc_dpcd)));
		seq_printf(m, "Force_DSC_Enable: %s\n",
			   yesno(intel_dp->force_dsc_en));
		if (!intel_dp_is_edp(intel_dp))
			seq_printf(m, "FEC_Sink_Support: %s\n",
				   yesno(drm_dp_sink_supports_fec(intel_dp->fec_capable)));
	} while (try_again);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

static ssize_t i915_dsc_fec_support_write(struct file *file,
					  const char __user *ubuf,
					  size_t len, loff_t *offp)
{
	bool dsc_enable = false;
	int ret;
	struct drm_connector *connector =
		((struct seq_file *)file->private_data)->private;
	struct intel_encoder *encoder = intel_attached_encoder(to_intel_connector(connector));
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	if (len == 0)
		return 0;

	drm_dbg(&i915->drm,
		"Copied %zu bytes from user to force DSC\n", len);

	ret = kstrtobool_from_user(ubuf, len, &dsc_enable);
	if (ret < 0)
		return ret;

	drm_dbg(&i915->drm, "Got %s for DSC Enable\n",
		(dsc_enable) ? "true" : "false");
	intel_dp->force_dsc_en = dsc_enable;

	*offp += len;
	return len;
}

static int i915_dsc_fec_support_open(struct inode *inode,
				     struct file *file)
{
	return single_open(file, i915_dsc_fec_support_show,
			   inode->i_private);
}

static const struct file_operations i915_dsc_fec_support_fops = {
	.owner = THIS_MODULE,
	.open = i915_dsc_fec_support_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_dsc_fec_support_write
};

static int i915_dsc_bpp_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	struct intel_encoder *encoder = intel_attached_encoder(to_intel_connector(connector));
	int ret;

	if (!encoder)
		return -ENODEV;

	ret = drm_modeset_lock_single_interruptible(&dev->mode_config.connection_mutex);
	if (ret)
		return ret;

	crtc = connector->state->crtc;
	if (connector->status != connector_status_connected || !crtc) {
		ret = -ENODEV;
		goto out;
	}

	crtc_state = to_intel_crtc_state(crtc->state);
	seq_printf(m, "Compressed_BPP: %d\n", crtc_state->dsc.compressed_bpp);

out:	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	return ret;
}

static ssize_t i915_dsc_bpp_write(struct file *file,
				  const char __user *ubuf,
				  size_t len, loff_t *offp)
{
	struct drm_connector *connector =
		((struct seq_file *)file->private_data)->private;
	struct intel_encoder *encoder = intel_attached_encoder(to_intel_connector(connector));
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	int dsc_bpp = 0;
	int ret;

	ret = kstrtoint_from_user(ubuf, len, 0, &dsc_bpp);
	if (ret < 0)
		return ret;

	intel_dp->force_dsc_bpp = dsc_bpp;
	*offp += len;

	return len;
}

static int i915_dsc_bpp_open(struct inode *inode,
			     struct file *file)
{
	return single_open(file, i915_dsc_bpp_show,
			   inode->i_private);
}

static const struct file_operations i915_dsc_bpp_fops = {
	.owner = THIS_MODULE,
	.open = i915_dsc_bpp_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_dsc_bpp_write
};

/**
 * intel_connector_debugfs_add - add i915 specific connector debugfs files
 * @connector: pointer to a registered drm_connector
 *
 * Cleanup will be done by drm_connector_unregister() through a call to
 * drm_debugfs_connector_remove().
 */
void intel_connector_debugfs_add(struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct dentry *root = connector->debugfs_entry;
	struct drm_i915_private *dev_priv = to_i915(connector->dev);

	/* The connector must have been registered beforehands. */
	if (!root)
		return;

	if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
		debugfs_create_file("i915_panel_timings", S_IRUGO, root,
				    connector, &i915_panel_fops);
		debugfs_create_file("i915_psr_sink_status", S_IRUGO, root,
				    connector, &i915_psr_sink_status_fops);
	}

	if (HAS_PSR(dev_priv) &&
	    connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
		debugfs_create_file("i915_psr_status", 0444, root,
				    connector, &i915_psr_status_fops);
	}

	if (connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector->connector_type == DRM_MODE_CONNECTOR_HDMIB) {
		debugfs_create_file("i915_hdcp_sink_capability", S_IRUGO, root,
				    connector, &i915_hdcp_sink_capability_fops);
	}

	if (DISPLAY_VER(dev_priv) >= 11 &&
	    ((connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort &&
	    !to_intel_connector(connector)->mst_port) ||
	    connector->connector_type == DRM_MODE_CONNECTOR_eDP)) {
		debugfs_create_file("i915_dsc_fec_support", 0644, root,
				    connector, &i915_dsc_fec_support_fops);

		debugfs_create_file("i915_dsc_bpp", 0644, root,
				    connector, &i915_dsc_bpp_fops);
	}

	if (connector->connector_type == DRM_MODE_CONNECTOR_DSI ||
	    connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
	    connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector->connector_type == DRM_MODE_CONNECTOR_HDMIB)
		debugfs_create_file("i915_lpsp_capability", 0444, root,
				    connector, &i915_lpsp_capability_fops);
}

/**
 * intel_crtc_debugfs_add - add i915 specific crtc debugfs files
 * @crtc: pointer to a drm_crtc
 *
 * Failure to add debugfs entries should generally be ignored.
 */
void intel_crtc_debugfs_add(struct drm_crtc *crtc)
{
	if (!crtc->debugfs_entry)
		return;

	crtc_updates_add(crtc);
	intel_fbc_crtc_debugfs_add(to_intel_crtc(crtc));
}
