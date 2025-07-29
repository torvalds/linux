// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/debugfs.h>
#include <linux/string_choices.h>
#include <linux/string_helpers.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>

#include "hsw_ips.h"
#include "i915_reg.h"
#include "i9xx_wm_regs.h"
#include "intel_alpm.h"
#include "intel_bo.h"
#include "intel_crtc.h"
#include "intel_crtc_state_dump.h"
#include "intel_de.h"
#include "intel_display_debugfs.h"
#include "intel_display_debugfs_params.h"
#include "intel_display_power.h"
#include "intel_display_power_well.h"
#include "intel_display_regs.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dp_test.h"
#include "intel_drrs.h"
#include "intel_fb.h"
#include "intel_fbc.h"
#include "intel_fbdev.h"
#include "intel_hdcp.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_link_bw.h"
#include "intel_panel.h"
#include "intel_pps.h"
#include "intel_psr.h"
#include "intel_psr_regs.h"
#include "intel_vdsc.h"
#include "intel_wm.h"

static struct intel_display *node_to_intel_display(struct drm_info_node *node)
{
	return to_intel_display(node->minor->dev);
}

static int intel_display_caps(struct seq_file *m, void *data)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	drm_printf(&p, "PCH type: %d\n", INTEL_PCH_TYPE(display));

	intel_display_device_info_print(DISPLAY_INFO(display),
					DISPLAY_RUNTIME_INFO(display), &p);
	intel_display_params_dump(&display->params, display->drm->driver->name, &p);

	return 0;
}

static int i915_frontbuffer_tracking(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);

	spin_lock(&display->fb_tracking.lock);

	seq_printf(m, "FB tracking busy bits: 0x%08x\n",
		   display->fb_tracking.busy_bits);

	seq_printf(m, "FB tracking flip bits: 0x%08x\n",
		   display->fb_tracking.flip_bits);

	spin_unlock(&display->fb_tracking.lock);

	return 0;
}

static int i915_sr_status(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);
	intel_wakeref_t wakeref;
	bool sr_enabled = false;

	wakeref = intel_display_power_get(display, POWER_DOMAIN_INIT);

	if (DISPLAY_VER(display) >= 9)
		/* no global SR status; inspect per-plane WM */;
	else if (HAS_PCH_SPLIT(display))
		sr_enabled = intel_de_read(display, WM1_LP_ILK) & WM_LP_ENABLE;
	else if (display->platform.i965gm || display->platform.g4x ||
		 display->platform.i945g || display->platform.i945gm)
		sr_enabled = intel_de_read(display, FW_BLC_SELF) & FW_BLC_SELF_EN;
	else if (display->platform.i915gm)
		sr_enabled = intel_de_read(display, INSTPM) & INSTPM_SELF_EN;
	else if (display->platform.pineview)
		sr_enabled = intel_de_read(display, DSPFW3(display)) & PINEVIEW_SELF_REFRESH_EN;
	else if (display->platform.valleyview || display->platform.cherryview)
		sr_enabled = intel_de_read(display, FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;

	intel_display_power_put(display, POWER_DOMAIN_INIT, wakeref);

	seq_printf(m, "self-refresh: %s\n", str_enabled_disabled(sr_enabled));

	return 0;
}

static int i915_gem_framebuffer_info(struct seq_file *m, void *data)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct intel_framebuffer *fbdev_fb = NULL;
	struct drm_framebuffer *drm_fb;

	fbdev_fb = intel_fbdev_framebuffer(display->fbdev.fbdev);
	if (fbdev_fb) {
		seq_printf(m, "fbcon size: %d x %d, depth %d, %d bpp, modifier 0x%llx, refcount %d, obj ",
			   fbdev_fb->base.width,
			   fbdev_fb->base.height,
			   fbdev_fb->base.format->depth,
			   fbdev_fb->base.format->cpp[0] * 8,
			   fbdev_fb->base.modifier,
			   drm_framebuffer_read_refcount(&fbdev_fb->base));
		intel_bo_describe(m, intel_fb_bo(&fbdev_fb->base));
		seq_putc(m, '\n');
	}

	mutex_lock(&display->drm->mode_config.fb_lock);
	drm_for_each_fb(drm_fb, display->drm) {
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
		intel_bo_describe(m, intel_fb_bo(&fb->base));
		seq_putc(m, '\n');
	}
	mutex_unlock(&display->drm->mode_config.fb_lock);

	return 0;
}

static int i915_power_domain_info(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);

	intel_display_power_debug(display, m);

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
	struct intel_display *display = node_to_intel_display(m->private);
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	seq_printf(m, "\t[ENCODER:%d:%s]: connectors:\n",
		   encoder->base.base.id, encoder->base.name);

	drm_connector_list_iter_begin(display->drm, &conn_iter);
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

static void intel_panel_info(struct seq_file *m,
			     struct intel_connector *connector)
{
	const struct drm_display_mode *fixed_mode;

	if (list_empty(&connector->panel.fixed_modes))
		return;

	seq_puts(m, "\tfixed modes:\n");

	list_for_each_entry(fixed_mode, &connector->panel.fixed_modes, head)
		intel_seq_print_mode(m, 2, fixed_mode);
}

static void intel_dp_info(struct seq_file *m, struct intel_connector *connector)
{
	struct intel_encoder *intel_encoder = intel_attached_encoder(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(intel_encoder);

	seq_printf(m, "\tDPCD rev: %x\n", intel_dp->dpcd[DP_DPCD_REV]);
	seq_printf(m, "\taudio support: %s\n",
		   str_yes_no(connector->base.display_info.has_audio));

	drm_dp_downstream_debug(m, intel_dp->dpcd, intel_dp->downstream_ports,
				connector->detect_edid, &intel_dp->aux);
}

static void intel_dp_mst_info(struct seq_file *m,
			      struct intel_connector *connector)
{
	bool has_audio = connector->base.display_info.has_audio;

	seq_printf(m, "\taudio support: %s\n", str_yes_no(has_audio));
}

static void intel_hdmi_info(struct seq_file *m,
			    struct intel_connector *connector)
{
	bool has_audio = connector->base.display_info.has_audio;

	seq_printf(m, "\taudio support: %s\n", str_yes_no(has_audio));
}

static void intel_connector_info(struct seq_file *m,
				 struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
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

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		if (intel_connector->mst.dp)
			intel_dp_mst_info(m, intel_connector);
		else
			intel_dp_info(m, intel_connector);
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
		intel_hdmi_info(m, intel_connector);
		break;
	default:
		break;
	}

	intel_hdcp_info(m, intel_connector);

	seq_printf(m, "\tmax bpc: %u\n", connector->display_info.bpc);

	intel_panel_info(m, intel_connector);

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

	if (plane_state->is_y_plane)
		return "Y plane";

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
			   plane_state->is_y_plane ? "Y plane" : "UV plane");
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
		   str_yes_no(plane_state->uapi.visible),
		   DRM_RECT_FP_ARG(&plane_state->uapi.src),
		   DRM_RECT_ARG(&plane_state->uapi.dst),
		   rot_str);
}

static void intel_plane_info(struct seq_file *m, struct intel_crtc *crtc)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct intel_plane *plane;

	for_each_intel_plane_on_crtc(display->drm, crtc, plane) {
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

	/* Not all platforms have a scaler */
	if (num_scalers) {
		seq_printf(m, "\tnum_scalers=%d, scaler_users=%x scaler_id=%d scaling_filter=%d",
			   num_scalers,
			   crtc_state->scaler_state.scaler_users,
			   crtc_state->scaler_state.scaler_id,
			   crtc_state->hw.scaling_filter);

		for (i = 0; i < num_scalers; i++) {
			const struct intel_scaler *sc =
				&crtc_state->scaler_state.scalers[i];

			seq_printf(m, ", scalers[%d]: use=%s, mode=%x",
				   i, str_yes_no(sc->in_use), sc->mode);
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
		   hdr, div64_u64(crtc->debug.vbl.sum, count));
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

static void crtc_updates_add(struct intel_crtc *crtc)
{
	debugfs_create_file("i915_update_info", 0644, crtc->base.debugfs_entry,
			    crtc, &crtc_updates_fops);
}

#else
static void crtc_updates_info(struct seq_file *m,
			      struct intel_crtc *crtc,
			      const char *hdr)
{
}

static void crtc_updates_add(struct intel_crtc *crtc)
{
}
#endif

static void intel_crtc_info(struct seq_file *m, struct intel_crtc *crtc)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	const struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct intel_encoder *encoder;

	seq_printf(m, "[CRTC:%d:%s]:\n",
		   crtc->base.base.id, crtc->base.name);

	seq_printf(m, "\tuapi: enable=%s, active=%s, mode=" DRM_MODE_FMT "\n",
		   str_yes_no(crtc_state->uapi.enable),
		   str_yes_no(crtc_state->uapi.active),
		   DRM_MODE_ARG(&crtc_state->uapi.mode));

	seq_printf(m, "\thw: enable=%s, active=%s\n",
		   str_yes_no(crtc_state->hw.enable), str_yes_no(crtc_state->hw.active));
	seq_printf(m, "\tadjusted_mode=" DRM_MODE_FMT "\n",
		   DRM_MODE_ARG(&crtc_state->hw.adjusted_mode));
	seq_printf(m, "\tpipe__mode=" DRM_MODE_FMT "\n",
		   DRM_MODE_ARG(&crtc_state->hw.pipe_mode));

	seq_printf(m, "\tpipe src=" DRM_RECT_FMT ", dither=%s, bpp=%d\n",
		   DRM_RECT_ARG(&crtc_state->pipe_src),
		   str_yes_no(crtc_state->dither), crtc_state->pipe_bpp);
	seq_printf(m, "\tport_clock=%d, lane_count=%d\n",
		   crtc_state->port_clock, crtc_state->lane_count);

	intel_scaler_info(m, crtc);

	if (crtc_state->joiner_pipes)
		seq_printf(m, "\tLinked to 0x%x pipes as a %s\n",
			   crtc_state->joiner_pipes,
			   intel_crtc_is_joiner_secondary(crtc_state) ? "slave" : "master");

	intel_vdsc_state_dump(&p, 1, crtc_state);

	for_each_intel_encoder_mask(display->drm, encoder,
				    crtc_state->uapi.encoder_mask)
		intel_encoder_info(m, crtc, encoder);

	intel_plane_info(m, crtc);

	seq_printf(m, "\tunderrun reporting: cpu=%s pch=%s\n",
		   str_yes_no(!crtc->cpu_fifo_underrun_disabled),
		   str_yes_no(!crtc->pch_fifo_underrun_disabled));

	crtc_updates_info(m, crtc, "\t");
}

static int i915_display_info(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct intel_crtc *crtc;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct ref_tracker *wakeref;

	wakeref = intel_display_rpm_get(display);

	drm_modeset_lock_all(display->drm);

	seq_printf(m, "CRTC info\n");
	seq_printf(m, "---------\n");
	for_each_intel_crtc(display->drm, crtc)
		intel_crtc_info(m, crtc);

	seq_printf(m, "\n");
	seq_printf(m, "Connector info\n");
	seq_printf(m, "--------------\n");
	drm_connector_list_iter_begin(display->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		intel_connector_info(m, connector);
	drm_connector_list_iter_end(&conn_iter);

	drm_modeset_unlock_all(display->drm);

	intel_display_rpm_put(display, wakeref);

	return 0;
}

static int i915_shared_dplls_info(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	struct intel_dpll *pll;
	int i;

	drm_modeset_lock_all(display->drm);

	drm_printf(&p, "PLL refclks: non-SSC: %d kHz, SSC: %d kHz\n",
		   display->dpll.ref_clks.nssc,
		   display->dpll.ref_clks.ssc);

	for_each_dpll(display, pll, i) {
		drm_printf(&p, "DPLL%i: %s, id: %i\n", pll->index,
			   pll->info->name, pll->info->id);
		drm_printf(&p, " pipe_mask: 0x%x, active: 0x%x, on: %s\n",
			   pll->state.pipe_mask, pll->active_mask,
			   str_yes_no(pll->on));
		drm_printf(&p, " tracked hardware state:\n");
		intel_dpll_dump_hw_state(display, &p, &pll->state.hw_state);
	}
	drm_modeset_unlock_all(display->drm);

	return 0;
}

static int i915_ddb_info(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct skl_ddb_entry *entry;
	struct intel_crtc *crtc;

	if (DISPLAY_VER(display) < 9)
		return -ENODEV;

	drm_modeset_lock_all(display->drm);

	seq_printf(m, "%-15s%8s%8s%8s\n", "", "Start", "End", "Size");

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		enum pipe pipe = crtc->pipe;
		enum plane_id plane_id;

		seq_printf(m, "Pipe %c\n", pipe_name(pipe));

		for_each_plane_id_on_crtc(crtc, plane_id) {
			entry = &crtc_state->wm.skl.plane_ddb[plane_id];
			seq_printf(m, "  Plane%-8d%8u%8u%8u\n", plane_id + 1,
				   entry->start, entry->end,
				   skl_ddb_entry_size(entry));
		}

		entry = &crtc_state->wm.skl.plane_ddb[PLANE_CURSOR];
		seq_printf(m, "  %-13s%8u%8u%8u\n", "Cursor", entry->start,
			   entry->end, skl_ddb_entry_size(entry));
	}

	drm_modeset_unlock_all(display->drm);

	return 0;
}

static bool
intel_lpsp_power_well_enabled(struct intel_display *display,
			      enum i915_power_well_id power_well_id)
{
	bool is_enabled;

	with_intel_display_rpm(display)
		is_enabled = intel_display_power_well_is_enabled(display,
								 power_well_id);

	return is_enabled;
}

static int i915_lpsp_status(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);
	bool lpsp_enabled = false;

	if (DISPLAY_VER(display) >= 13 || IS_DISPLAY_VER(display, 9, 10)) {
		lpsp_enabled = !intel_lpsp_power_well_enabled(display, SKL_DISP_PW_2);
	} else if (IS_DISPLAY_VER(display, 11, 12)) {
		lpsp_enabled = !intel_lpsp_power_well_enabled(display, ICL_DISP_PW_3);
	} else if (display->platform.haswell || display->platform.broadwell) {
		lpsp_enabled = !intel_lpsp_power_well_enabled(display, HSW_DISP_PW_GLOBAL);
	} else {
		seq_puts(m, "LPSP: not supported\n");
		return 0;
	}

	seq_printf(m, "LPSP: %s\n", str_enabled_disabled(lpsp_enabled));

	return 0;
}

static int i915_dp_mst_info(struct seq_file *m, void *unused)
{
	struct intel_display *display = node_to_intel_display(m->private);
	struct intel_encoder *intel_encoder;
	struct intel_digital_port *dig_port;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
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
		drm_dp_mst_dump_topology(m, &dig_port->dp.mst.mgr);
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static ssize_t
i915_fifo_underrun_reset_write(struct file *filp,
			       const char __user *ubuf,
			       size_t cnt, loff_t *ppos)
{
	struct intel_display *display = filp->private_data;
	struct intel_crtc *crtc;
	int ret;
	bool reset;

	ret = kstrtobool_from_user(ubuf, cnt, &reset);
	if (ret)
		return ret;

	if (!reset)
		return cnt;

	for_each_intel_crtc(display->drm, crtc) {
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
			drm_dbg_kms(display->drm,
				    "Re-arming FIFO underruns on pipe %c\n",
				    pipe_name(crtc->pipe));

			intel_crtc_arm_fifo_underrun(crtc, crtc_state);
		}

		drm_modeset_unlock(&crtc->base.mutex);

		if (ret)
			return ret;
	}

	intel_fbc_reset_underrun(display);

	return cnt;
}

static const struct file_operations i915_fifo_underrun_reset_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = i915_fifo_underrun_reset_write,
	.llseek = default_llseek,
};

static const struct drm_info_list intel_display_debugfs_list[] = {
	{"intel_display_caps", intel_display_caps, 0},
	{"i915_frontbuffer_tracking", i915_frontbuffer_tracking, 0},
	{"i915_sr_status", i915_sr_status, 0},
	{"i915_gem_framebuffer", i915_gem_framebuffer_info, 0},
	{"i915_power_domain_info", i915_power_domain_info, 0},
	{"i915_display_info", i915_display_info, 0},
	{"i915_shared_dplls_info", i915_shared_dplls_info, 0},
	{"i915_dp_mst_info", i915_dp_mst_info, 0},
	{"i915_ddb_info", i915_ddb_info, 0},
	{"i915_lpsp_status", i915_lpsp_status, 0},
};

void intel_display_debugfs_register(struct intel_display *display)
{
	struct dentry *debugfs_root = display->drm->debugfs_root;

	debugfs_create_file("i915_fifo_underrun_reset", 0644, debugfs_root,
			    display, &i915_fifo_underrun_reset_ops);

	drm_debugfs_create_files(intel_display_debugfs_list,
				 ARRAY_SIZE(intel_display_debugfs_list),
				 debugfs_root, display->drm->primary);

	intel_bios_debugfs_register(display);
	intel_cdclk_debugfs_register(display);
	intel_dmc_debugfs_register(display);
	intel_dp_test_debugfs_register(display);
	intel_fbc_debugfs_register(display);
	intel_hpd_debugfs_register(display);
	intel_opregion_debugfs_register(display);
	intel_psr_debugfs_register(display);
	intel_wm_debugfs_register(display);
	intel_display_debugfs_params(display);
}

static int i915_lpsp_capability_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	int connector_type = connector->base.connector_type;
	bool lpsp_capable = false;

	if (!encoder)
		return -ENODEV;

	if (connector->base.status != connector_status_connected)
		return -ENODEV;

	if (DISPLAY_VER(display) >= 13)
		lpsp_capable = encoder->port <= PORT_B;
	else if (DISPLAY_VER(display) >= 12)
		/*
		 * Actually TGL can drive LPSP on port till DDI_C
		 * but there is no physical connected DDI_C on TGL sku's,
		 * even driver is not initializing DDI_C port for gen12.
		 */
		lpsp_capable = encoder->port <= PORT_B;
	else if (DISPLAY_VER(display) == 11)
		lpsp_capable = (connector_type == DRM_MODE_CONNECTOR_DSI ||
				connector_type == DRM_MODE_CONNECTOR_eDP);
	else if (IS_DISPLAY_VER(display, 9, 10))
		lpsp_capable = (encoder->port == PORT_A &&
				(connector_type == DRM_MODE_CONNECTOR_DSI ||
				 connector_type == DRM_MODE_CONNECTOR_eDP ||
				 connector_type == DRM_MODE_CONNECTOR_DisplayPort));
	else if (display->platform.haswell || display->platform.broadwell)
		lpsp_capable = connector_type == DRM_MODE_CONNECTOR_eDP;

	seq_printf(m, "LPSP: %s\n", lpsp_capable ? "capable" : "incapable");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_lpsp_capability);

static int i915_dsc_fec_support_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct drm_crtc *crtc;
	struct intel_dp *intel_dp;
	struct drm_modeset_acquire_ctx ctx;
	struct intel_crtc_state *crtc_state = NULL;
	int ret = 0;
	bool try_again = false;

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);

	do {
		try_again = false;
		ret = drm_modeset_lock(&display->drm->mode_config.connection_mutex,
				       &ctx);
		if (ret) {
			if (ret == -EDEADLK && !drm_modeset_backoff(&ctx)) {
				try_again = true;
				continue;
			}
			break;
		}
		crtc = connector->base.state->crtc;
		if (connector->base.status != connector_status_connected || !crtc) {
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
		intel_dp = intel_attached_dp(connector);
		crtc_state = to_intel_crtc_state(crtc->state);
		seq_printf(m, "DSC_Enabled: %s\n",
			   str_yes_no(crtc_state->dsc.compression_enable));
		seq_printf(m, "DSC_Sink_Support: %s\n",
			   str_yes_no(drm_dp_sink_supports_dsc(connector->dp.dsc_dpcd)));
		seq_printf(m, "DSC_Output_Format_Sink_Support: RGB: %s YCBCR420: %s YCBCR444: %s\n",
			   str_yes_no(drm_dp_dsc_sink_supports_format(connector->dp.dsc_dpcd,
								      DP_DSC_RGB)),
			   str_yes_no(drm_dp_dsc_sink_supports_format(connector->dp.dsc_dpcd,
								      DP_DSC_YCbCr420_Native)),
			   str_yes_no(drm_dp_dsc_sink_supports_format(connector->dp.dsc_dpcd,
								      DP_DSC_YCbCr444)));
		seq_printf(m, "DSC_Sink_BPP_Precision: %d\n",
			   drm_dp_dsc_sink_bpp_incr(connector->dp.dsc_dpcd));
		seq_printf(m, "DSC_Sink_Max_Slice_Count: %d\n",
			   drm_dp_dsc_sink_max_slice_count((connector->dp.dsc_dpcd), intel_dp_is_edp(intel_dp)));
		seq_printf(m, "Force_DSC_Enable: %s\n",
			   str_yes_no(intel_dp->force_dsc_en));
		if (!intel_dp_is_edp(intel_dp))
			seq_printf(m, "FEC_Sink_Support: %s\n",
				   str_yes_no(drm_dp_sink_supports_fec(connector->dp.fec_capability)));
	} while (try_again);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

static ssize_t i915_dsc_fec_support_write(struct file *file,
					  const char __user *ubuf,
					  size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	bool dsc_enable = false;
	int ret;

	if (len == 0)
		return 0;

	drm_dbg(display->drm,
		"Copied %zu bytes from user to force DSC\n", len);

	ret = kstrtobool_from_user(ubuf, len, &dsc_enable);
	if (ret < 0)
		return ret;

	drm_dbg(display->drm, "Got %s for DSC Enable\n",
		str_true_false(dsc_enable));
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

static int i915_dsc_bpc_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct drm_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	int ret;

	if (!encoder)
		return -ENODEV;

	ret = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (ret)
		return ret;

	crtc = connector->base.state->crtc;
	if (connector->base.status != connector_status_connected || !crtc) {
		ret = -ENODEV;
		goto out;
	}

	crtc_state = to_intel_crtc_state(crtc->state);
	seq_printf(m, "Input_BPC: %d\n", crtc_state->dsc.config.bits_per_component);

out:	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return ret;
}

static ssize_t i915_dsc_bpc_write(struct file *file,
				  const char __user *ubuf,
				  size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = m->private;
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	int dsc_bpc = 0;
	int ret;

	ret = kstrtoint_from_user(ubuf, len, 0, &dsc_bpc);
	if (ret < 0)
		return ret;

	intel_dp->force_dsc_bpc = dsc_bpc;
	*offp += len;

	return len;
}

static int i915_dsc_bpc_open(struct inode *inode,
			     struct file *file)
{
	return single_open(file, i915_dsc_bpc_show, inode->i_private);
}

static const struct file_operations i915_dsc_bpc_fops = {
	.owner = THIS_MODULE,
	.open = i915_dsc_bpc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_dsc_bpc_write
};

static int i915_dsc_output_format_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct drm_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	int ret;

	if (!encoder)
		return -ENODEV;

	ret = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (ret)
		return ret;

	crtc = connector->base.state->crtc;
	if (connector->base.status != connector_status_connected || !crtc) {
		ret = -ENODEV;
		goto out;
	}

	crtc_state = to_intel_crtc_state(crtc->state);
	seq_printf(m, "DSC_Output_Format: %s\n",
		   intel_output_format_name(crtc_state->output_format));

out:	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return ret;
}

static ssize_t i915_dsc_output_format_write(struct file *file,
					    const char __user *ubuf,
					    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = m->private;
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	int dsc_output_format = 0;
	int ret;

	ret = kstrtoint_from_user(ubuf, len, 0, &dsc_output_format);
	if (ret < 0)
		return ret;

	intel_dp->force_dsc_output_format = dsc_output_format;
	*offp += len;

	return len;
}

static int i915_dsc_output_format_open(struct inode *inode,
				       struct file *file)
{
	return single_open(file, i915_dsc_output_format_show, inode->i_private);
}

static const struct file_operations i915_dsc_output_format_fops = {
	.owner = THIS_MODULE,
	.open = i915_dsc_output_format_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_dsc_output_format_write
};

static int i915_dsc_fractional_bpp_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct drm_crtc *crtc;
	struct intel_dp *intel_dp;
	int ret;

	if (!encoder)
		return -ENODEV;

	ret = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (ret)
		return ret;

	crtc = connector->base.state->crtc;
	if (connector->base.status != connector_status_connected || !crtc) {
		ret = -ENODEV;
		goto out;
	}

	intel_dp = intel_attached_dp(connector);
	seq_printf(m, "Force_DSC_Fractional_BPP_Enable: %s\n",
		   str_yes_no(intel_dp->force_dsc_fractional_bpp_en));

out:
	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return ret;
}

static ssize_t i915_dsc_fractional_bpp_write(struct file *file,
					     const char __user *ubuf,
					     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	bool dsc_fractional_bpp_enable = false;
	int ret;

	if (len == 0)
		return 0;

	drm_dbg(display->drm,
		"Copied %zu bytes from user to force fractional bpp for DSC\n", len);

	ret = kstrtobool_from_user(ubuf, len, &dsc_fractional_bpp_enable);
	if (ret < 0)
		return ret;

	drm_dbg(display->drm, "Got %s for DSC Fractional BPP Enable\n",
		str_true_false(dsc_fractional_bpp_enable));
	intel_dp->force_dsc_fractional_bpp_en = dsc_fractional_bpp_enable;

	*offp += len;

	return len;
}

static int i915_dsc_fractional_bpp_open(struct inode *inode,
					struct file *file)
{
	return single_open(file, i915_dsc_fractional_bpp_show, inode->i_private);
}

static const struct file_operations i915_dsc_fractional_bpp_fops = {
	.owner = THIS_MODULE,
	.open = i915_dsc_fractional_bpp_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_dsc_fractional_bpp_write
};

/*
 * Returns the Current CRTC's bpc.
 * Example usage: cat /sys/kernel/debug/dri/0/crtc-0/i915_current_bpc
 */
static int i915_current_bpc_show(struct seq_file *m, void *data)
{
	struct intel_crtc *crtc = m->private;
	struct intel_crtc_state *crtc_state;
	int ret;

	ret = drm_modeset_lock_single_interruptible(&crtc->base.mutex);
	if (ret)
		return ret;

	crtc_state = to_intel_crtc_state(crtc->base.state);
	seq_printf(m, "Current: %u\n", crtc_state->pipe_bpp / 3);

	drm_modeset_unlock(&crtc->base.mutex);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(i915_current_bpc);

/* Pipe may differ from crtc index if pipes are fused off */
static int intel_crtc_pipe_show(struct seq_file *m, void *unused)
{
	struct intel_crtc *crtc = m->private;

	seq_printf(m, "%c\n", pipe_name(crtc->pipe));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(intel_crtc_pipe);

static int i915_joiner_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;

	seq_printf(m, "%d\n", connector->force_joined_pipes);

	return 0;
}

static ssize_t i915_joiner_write(struct file *file,
				 const char __user *ubuf,
				 size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	int force_joined_pipes = 0;
	int ret;

	if (len == 0)
		return 0;

	ret = kstrtoint_from_user(ubuf, len, 0, &force_joined_pipes);
	if (ret < 0)
		return ret;

	switch (force_joined_pipes) {
	case 0:
	case 1:
	case 2:
		connector->force_joined_pipes = force_joined_pipes;
		break;
	case 4:
		if (HAS_ULTRAJOINER(display)) {
			connector->force_joined_pipes = force_joined_pipes;
			break;
		}

		fallthrough;
	default:
		return -EINVAL;
	}

	*offp += len;

	return len;
}

static int i915_joiner_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_joiner_show, inode->i_private);
}

static const struct file_operations i915_joiner_fops = {
	.owner = THIS_MODULE,
	.open = i915_joiner_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_joiner_write
};

/**
 * intel_connector_debugfs_add - add i915 specific connector debugfs files
 * @connector: pointer to a registered intel_connector
 *
 * Cleanup will be done by drm_connector_unregister() through a call to
 * drm_debugfs_connector_remove().
 */
void intel_connector_debugfs_add(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct dentry *root = connector->base.debugfs_entry;
	int connector_type = connector->base.connector_type;

	/* The connector must have been registered beforehands. */
	if (!root)
		return;

	intel_drrs_connector_debugfs_add(connector);
	intel_hdcp_connector_debugfs_add(connector);
	intel_pps_connector_debugfs_add(connector);
	intel_psr_connector_debugfs_add(connector);
	intel_alpm_lobf_debugfs_add(connector);
	intel_dp_link_training_debugfs_add(connector);
	intel_link_bw_connector_debugfs_add(connector);

	if (DISPLAY_VER(display) >= 11 &&
	    ((connector_type == DRM_MODE_CONNECTOR_DisplayPort && !connector->mst.dp) ||
	     connector_type == DRM_MODE_CONNECTOR_eDP)) {
		debugfs_create_file("i915_dsc_fec_support", 0644, root,
				    connector, &i915_dsc_fec_support_fops);

		debugfs_create_file("i915_dsc_bpc", 0644, root,
				    connector, &i915_dsc_bpc_fops);

		debugfs_create_file("i915_dsc_output_format", 0644, root,
				    connector, &i915_dsc_output_format_fops);

		debugfs_create_file("i915_dsc_fractional_bpp", 0644, root,
				    connector, &i915_dsc_fractional_bpp_fops);
	}

	if ((connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	     connector_type == DRM_MODE_CONNECTOR_eDP) &&
	    intel_dp_has_joiner(intel_attached_dp(connector))) {
		debugfs_create_file("i915_joiner_force_enable", 0644, root,
				    connector, &i915_joiner_fops);
	}

	if (connector_type == DRM_MODE_CONNECTOR_DSI ||
	    connector_type == DRM_MODE_CONNECTOR_eDP ||
	    connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_HDMIB)
		debugfs_create_file("i915_lpsp_capability", 0444, root,
				    connector, &i915_lpsp_capability_fops);
}

/**
 * intel_crtc_debugfs_add - add i915 specific crtc debugfs files
 * @crtc: pointer to a drm_crtc
 *
 * Failure to add debugfs entries should generally be ignored.
 */
void intel_crtc_debugfs_add(struct intel_crtc *crtc)
{
	struct dentry *root = crtc->base.debugfs_entry;

	if (!root)
		return;

	crtc_updates_add(crtc);
	intel_drrs_crtc_debugfs_add(crtc);
	intel_fbc_crtc_debugfs_add(crtc);
	hsw_ips_crtc_debugfs_add(crtc);

	debugfs_create_file("i915_current_bpc", 0444, root, crtc,
			    &i915_current_bpc_fops);
	debugfs_create_file("i915_pipe", 0444, root, crtc,
			    &intel_crtc_pipe_fops);
}
