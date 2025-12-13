// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022-2023 Intel Corporation
 *
 * High level display driver entry points. This is a layer between top level
 * driver code and low level display functionality; no low level display code or
 * details here.
 */

#include <linux/vga_switcheroo.h>
#include <acpi/video.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_client_event.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_privacy_screen_consumer.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "i915_drv.h"
#include "i915_utils.h"
#include "i9xx_wm.h"
#include "intel_acpi.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_bios.h"
#include "intel_bw.h"
#include "intel_cdclk.h"
#include "intel_color.h"
#include "intel_crtc.h"
#include "intel_display_core.h"
#include "intel_display_debugfs.h"
#include "intel_display_driver.h"
#include "intel_display_irq.h"
#include "intel_display_power.h"
#include "intel_display_types.h"
#include "intel_display_wa.h"
#include "intel_dkl_phy.h"
#include "intel_dmc.h"
#include "intel_dp.h"
#include "intel_dp_tunnel.h"
#include "intel_dpll.h"
#include "intel_dpll_mgr.h"
#include "intel_fb.h"
#include "intel_fbc.h"
#include "intel_fbdev.h"
#include "intel_fdi.h"
#include "intel_flipq.h"
#include "intel_gmbus.h"
#include "intel_hdcp.h"
#include "intel_hotplug.h"
#include "intel_hti.h"
#include "intel_modeset_lock.h"
#include "intel_modeset_setup.h"
#include "intel_opregion.h"
#include "intel_overlay.h"
#include "intel_plane_initial.h"
#include "intel_pmdemand.h"
#include "intel_pps.h"
#include "intel_psr.h"
#include "intel_quirks.h"
#include "intel_vga.h"
#include "intel_wm.h"
#include "skl_watermark.h"

bool intel_display_driver_probe_defer(struct pci_dev *pdev)
{
	struct drm_privacy_screen *privacy_screen;

	/*
	 * apple-gmux is needed on dual GPU MacBook Pro
	 * to probe the panel if we're the inactive GPU.
	 */
	if (vga_switcheroo_client_probe_defer(pdev))
		return true;

	/* If the LCD panel has a privacy-screen, wait for it */
	privacy_screen = drm_privacy_screen_get(&pdev->dev, NULL);
	if (IS_ERR(privacy_screen) && PTR_ERR(privacy_screen) == -EPROBE_DEFER)
		return true;

	drm_privacy_screen_put(privacy_screen);

	return false;
}

void intel_display_driver_init_hw(struct intel_display *display)
{
	if (!HAS_DISPLAY(display))
		return;

	intel_cdclk_read_hw(display);

	intel_display_wa_apply(display);
}

static const struct drm_mode_config_funcs intel_mode_funcs = {
	.fb_create = intel_user_framebuffer_create,
	.get_format_info = intel_fb_get_format_info,
	.mode_valid = intel_mode_valid,
	.atomic_check = intel_atomic_check,
	.atomic_commit = intel_atomic_commit,
	.atomic_state_alloc = intel_atomic_state_alloc,
	.atomic_state_clear = intel_atomic_state_clear,
	.atomic_state_free = intel_atomic_state_free,
};

static const struct drm_mode_config_helper_funcs intel_mode_config_funcs = {
	.atomic_commit_setup = drm_dp_mst_atomic_setup_commit,
};

static void intel_mode_config_init(struct intel_display *display)
{
	struct drm_mode_config *mode_config = &display->drm->mode_config;

	drm_mode_config_init(display->drm);
	INIT_LIST_HEAD(&display->global.obj_list);

	mode_config->min_width = 0;
	mode_config->min_height = 0;

	mode_config->preferred_depth = 24;
	mode_config->prefer_shadow = 1;

	mode_config->funcs = &intel_mode_funcs;
	mode_config->helper_private = &intel_mode_config_funcs;

	mode_config->async_page_flip = HAS_ASYNC_FLIPS(display);

	/*
	 * Maximum framebuffer dimensions, chosen to match
	 * the maximum render engine surface size on gen4+.
	 */
	if (DISPLAY_VER(display) >= 7) {
		mode_config->max_width = 16384;
		mode_config->max_height = 16384;
	} else if (DISPLAY_VER(display) >= 4) {
		mode_config->max_width = 8192;
		mode_config->max_height = 8192;
	} else if (DISPLAY_VER(display) == 3) {
		mode_config->max_width = 4096;
		mode_config->max_height = 4096;
	} else {
		mode_config->max_width = 2048;
		mode_config->max_height = 2048;
	}

	if (display->platform.i845g || display->platform.i865g) {
		mode_config->cursor_width = display->platform.i845g ? 64 : 512;
		mode_config->cursor_height = 1023;
	} else if (display->platform.i830 || display->platform.i85x ||
		   display->platform.i915g || display->platform.i915gm) {
		mode_config->cursor_width = 64;
		mode_config->cursor_height = 64;
	} else {
		mode_config->cursor_width = 256;
		mode_config->cursor_height = 256;
	}
}

static void intel_mode_config_cleanup(struct intel_display *display)
{
	intel_atomic_global_obj_cleanup(display);
	drm_mode_config_cleanup(display->drm);
}

static void intel_plane_possible_crtcs_init(struct intel_display *display)
{
	struct intel_plane *plane;

	for_each_intel_plane(display->drm, plane) {
		struct intel_crtc *crtc = intel_crtc_for_pipe(display,
							      plane->pipe);

		plane->base.possible_crtcs = drm_crtc_mask(&crtc->base);
	}
}

void intel_display_driver_early_probe(struct intel_display *display)
{
	/* This must be called before any calls to HAS_PCH_* */
	intel_pch_detect(display);

	if (!HAS_DISPLAY(display))
		return;

	spin_lock_init(&display->fb_tracking.lock);
	mutex_init(&display->backlight.lock);
	mutex_init(&display->audio.mutex);
	mutex_init(&display->wm.wm_mutex);
	mutex_init(&display->pps.mutex);
	mutex_init(&display->hdcp.hdcp_mutex);

	intel_display_irq_init(display);
	intel_dkl_phy_init(display);
	intel_color_init_hooks(display);
	intel_init_cdclk_hooks(display);
	intel_audio_hooks_init(display);
	intel_dpll_init_clock_hook(display);
	intel_init_display_hooks(display);
	intel_fdi_init_hook(display);
	intel_dmc_wl_init(display);
}

/* part #1: call before irq install */
int intel_display_driver_probe_noirq(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	int ret;

	if (i915_inject_probe_failure(i915))
		return -ENODEV;

	if (HAS_DISPLAY(display)) {
		ret = drm_vblank_init(display->drm,
				      INTEL_NUM_PIPES(display));
		if (ret)
			return ret;
	}

	intel_bios_init(display);

	ret = intel_vga_register(display);
	if (ret)
		goto cleanup_bios;

	intel_psr_dc5_dc6_wa_init(display);

	/* FIXME: completely on the wrong abstraction layer */
	ret = intel_power_domains_init(display);
	if (ret < 0)
		goto cleanup_vga;

	intel_pmdemand_init_early(display);

	intel_power_domains_init_hw(display, false);

	if (!HAS_DISPLAY(display))
		return 0;

	display->hotplug.dp_wq = alloc_ordered_workqueue("intel-dp", 0);
	if (!display->hotplug.dp_wq) {
		ret = -ENOMEM;
		goto cleanup_vga_client_pw_domain_dmc;
	}

	display->wq.modeset = alloc_ordered_workqueue("i915_modeset", 0);
	if (!display->wq.modeset) {
		ret = -ENOMEM;
		goto cleanup_wq_dp;
	}

	display->wq.flip = alloc_workqueue("i915_flip", WQ_HIGHPRI |
						WQ_UNBOUND, WQ_UNBOUND_MAX_ACTIVE);
	if (!display->wq.flip) {
		ret = -ENOMEM;
		goto cleanup_wq_modeset;
	}

	display->wq.cleanup = alloc_workqueue("i915_cleanup", WQ_HIGHPRI, 0);
	if (!display->wq.cleanup) {
		ret = -ENOMEM;
		goto cleanup_wq_flip;
	}

	display->wq.unordered = alloc_workqueue("display_unordered", 0, 0);
	if (!display->wq.unordered) {
		ret = -ENOMEM;
		goto cleanup_wq_cleanup;
	}

	intel_dmc_init(display);

	intel_mode_config_init(display);

	ret = intel_cdclk_init(display);
	if (ret)
		goto cleanup_wq_unordered;

	ret = intel_color_init(display);
	if (ret)
		goto cleanup_wq_unordered;

	ret = intel_dbuf_init(display);
	if (ret)
		goto cleanup_wq_unordered;

	ret = intel_bw_init(display);
	if (ret)
		goto cleanup_wq_unordered;

	ret = intel_pmdemand_init(display);
	if (ret)
		goto cleanup_wq_unordered;

	intel_init_quirks(display);

	intel_fbc_init(display);

	return 0;

cleanup_wq_unordered:
	destroy_workqueue(display->wq.unordered);
cleanup_wq_cleanup:
	destroy_workqueue(display->wq.cleanup);
cleanup_wq_flip:
	destroy_workqueue(display->wq.flip);
cleanup_wq_modeset:
	destroy_workqueue(display->wq.modeset);
cleanup_wq_dp:
	destroy_workqueue(display->hotplug.dp_wq);
cleanup_vga_client_pw_domain_dmc:
	intel_dmc_fini(display);
	intel_power_domains_driver_remove(display);
cleanup_vga:
	intel_vga_unregister(display);
cleanup_bios:
	intel_bios_driver_remove(display);

	return ret;
}

static void set_display_access(struct intel_display *display,
			       bool any_task_allowed,
			       struct task_struct *allowed_task)
{
	struct drm_modeset_acquire_ctx ctx;
	int err;

	intel_modeset_lock_ctx_retry(&ctx, NULL, 0, err) {
		err = drm_modeset_lock_all_ctx(display->drm, &ctx);
		if (err)
			continue;

		display->access.any_task_allowed = any_task_allowed;
		display->access.allowed_task = allowed_task;
	}

	drm_WARN_ON(display->drm, err);
}

/**
 * intel_display_driver_enable_user_access - Enable display HW access for all threads
 * @display: display device instance
 *
 * Enable the display HW access for all threads. Examples for such accesses
 * are modeset commits and connector probing.
 *
 * This function should be called during driver loading and system resume once
 * all the HW initialization steps are done.
 */
void intel_display_driver_enable_user_access(struct intel_display *display)
{
	set_display_access(display, true, NULL);

	intel_hpd_enable_detection_work(display);
}

/**
 * intel_display_driver_disable_user_access - Disable display HW access for user threads
 * @display: display device instance
 *
 * Disable the display HW access for user threads. Examples for such accesses
 * are modeset commits and connector probing. For the current thread the
 * access is still enabled, which should only perform HW init/deinit
 * programming (as the initial modeset during driver loading or the disabling
 * modeset during driver unloading and system suspend/shutdown). This function
 * should be followed by calling either intel_display_driver_enable_user_access()
 * after completing the HW init programming or
 * intel_display_driver_suspend_access() after completing the HW deinit
 * programming.
 *
 * This function should be called during driver loading/unloading and system
 * suspend/shutdown before starting the HW init/deinit programming.
 */
void intel_display_driver_disable_user_access(struct intel_display *display)
{
	intel_hpd_disable_detection_work(display);

	set_display_access(display, false, current);
}

/**
 * intel_display_driver_suspend_access - Suspend display HW access for all threads
 * @display: display device instance
 *
 * Disable the display HW access for all threads. Examples for such accesses
 * are modeset commits and connector probing. This call should be either
 * followed by calling intel_display_driver_resume_access(), or the driver
 * should be unloaded/shutdown.
 *
 * This function should be called during driver unloading and system
 * suspend/shutdown after completing the HW deinit programming.
 */
void intel_display_driver_suspend_access(struct intel_display *display)
{
	set_display_access(display, false, NULL);
}

/**
 * intel_display_driver_resume_access - Resume display HW access for the resume thread
 * @display: display device instance
 *
 * Enable the display HW access for the current resume thread, keeping the
 * access disabled for all other (user) threads. Examples for such accesses
 * are modeset commits and connector probing. The resume thread should only
 * perform HW init programming (as the restoring modeset). This function
 * should be followed by calling intel_display_driver_enable_user_access(),
 * after completing the HW init programming steps.
 *
 * This function should be called during system resume before starting the HW
 * init steps.
 */
void intel_display_driver_resume_access(struct intel_display *display)
{
	set_display_access(display, false, current);
}

/**
 * intel_display_driver_check_access - Check if the current thread has disaplay HW access
 * @display: display device instance
 *
 * Check whether the current thread has display HW access, print a debug
 * message if it doesn't. Such accesses are modeset commits and connector
 * probing. If the function returns %false any HW access should be prevented.
 *
 * Returns %true if the current thread has display HW access, %false
 * otherwise.
 */
bool intel_display_driver_check_access(struct intel_display *display)
{
	char current_task[TASK_COMM_LEN + 16];
	char allowed_task[TASK_COMM_LEN + 16] = "none";

	if (display->access.any_task_allowed ||
	    display->access.allowed_task == current)
		return true;

	snprintf(current_task, sizeof(current_task), "%s[%d]",
		 current->comm, task_pid_vnr(current));

	if (display->access.allowed_task)
		snprintf(allowed_task, sizeof(allowed_task), "%s[%d]",
			 display->access.allowed_task->comm,
			 task_pid_vnr(display->access.allowed_task));

	drm_dbg_kms(display->drm,
		    "Reject display access from task %s (allowed to %s)\n",
		    current_task, allowed_task);

	return false;
}

/* part #2: call after irq install, but before gem init */
int intel_display_driver_probe_nogem(struct intel_display *display)
{
	enum pipe pipe;
	int ret;

	if (!HAS_DISPLAY(display))
		return 0;

	intel_wm_init(display);

	intel_panel_sanitize_ssc(display);

	intel_pps_setup(display);

	intel_gmbus_setup(display);

	drm_dbg_kms(display->drm, "%d display pipe%s available.\n",
		    INTEL_NUM_PIPES(display),
		    INTEL_NUM_PIPES(display) > 1 ? "s" : "");

	for_each_pipe(display, pipe) {
		ret = intel_crtc_init(display, pipe);
		if (ret)
			goto err_mode_config;
	}

	intel_plane_possible_crtcs_init(display);
	intel_dpll_init(display);
	intel_fdi_pll_freq_update(display);

	intel_update_czclk(display);
	intel_display_driver_init_hw(display);
	intel_dpll_update_ref_clks(display);

	if (display->cdclk.max_cdclk_freq == 0)
		intel_update_max_cdclk(display);

	intel_hti_init(display);

	intel_setup_outputs(display);

	ret = intel_dp_tunnel_mgr_init(display);
	if (ret)
		goto err_hdcp;

	intel_display_driver_disable_user_access(display);

	drm_modeset_lock_all(display->drm);
	intel_modeset_setup_hw_state(display, display->drm->mode_config.acquire_ctx);
	intel_acpi_assign_connector_fwnodes(display);
	drm_modeset_unlock_all(display->drm);

	intel_initial_plane_config(display);

	/*
	 * Make sure hardware watermarks really match the state we read out.
	 * Note that we need to do this after reconstructing the BIOS fb's
	 * since the watermark calculation done here will use pstate->fb.
	 */
	if (!HAS_GMCH(display))
		ilk_wm_sanitize(display);

	return 0;

err_hdcp:
	intel_hdcp_component_fini(display);
err_mode_config:
	intel_mode_config_cleanup(display);

	return ret;
}

/* part #3: call after gem init */
int intel_display_driver_probe(struct intel_display *display)
{
	int ret;

	if (!HAS_DISPLAY(display))
		return 0;

	/*
	 * This will bind stuff into ggtt, so it needs to be done after
	 * the BIOS fb takeover and whatever else magic ggtt reservations
	 * happen during gem/ggtt init.
	 */
	intel_hdcp_component_init(display);

	intel_flipq_init(display);

	/*
	 * Force all active planes to recompute their states. So that on
	 * mode_setcrtc after probe, all the intel_plane_state variables
	 * are already calculated and there is no assert_plane warnings
	 * during bootup.
	 */
	ret = intel_initial_commit(display);
	if (ret)
		drm_dbg_kms(display->drm, "Initial modeset failed, %d\n", ret);

	intel_overlay_setup(display);

	/* Only enable hotplug handling once the fbdev is fully set up. */
	intel_hpd_init(display);

	skl_watermark_ipc_init(display);

	return 0;
}

void intel_display_driver_register(struct intel_display *display)
{
	struct drm_printer p = drm_dbg_printer(display->drm, DRM_UT_KMS,
					       "i915 display info:");

	if (!HAS_DISPLAY(display))
		return;

	/* Must be done after probing outputs */
	intel_opregion_register(display);
	intel_acpi_video_register(display);

	intel_audio_init(display);

	intel_display_driver_enable_user_access(display);

	intel_audio_register(display);

	intel_display_debugfs_register(display);

	/*
	 * We need to coordinate the hotplugs with the asynchronous
	 * fbdev configuration, for which we use the
	 * fbdev->async_cookie.
	 */
	drm_kms_helper_poll_init(display->drm);
	intel_hpd_poll_disable(display);

	intel_fbdev_setup(display);

	intel_display_device_info_print(DISPLAY_INFO(display),
					DISPLAY_RUNTIME_INFO(display), &p);

	intel_register_dsm_handler();
}

/* part #1: call before irq uninstall */
void intel_display_driver_remove(struct intel_display *display)
{
	if (!HAS_DISPLAY(display))
		return;

	flush_workqueue(display->wq.flip);
	flush_workqueue(display->wq.modeset);
	flush_workqueue(display->wq.cleanup);
	flush_workqueue(display->wq.unordered);

	/*
	 * MST topology needs to be suspended so we don't have any calls to
	 * fbdev after it's finalized. MST will be destroyed later as part of
	 * drm_mode_config_cleanup()
	 */
	intel_dp_mst_suspend(display);
}

/* part #2: call after irq uninstall */
void intel_display_driver_remove_noirq(struct intel_display *display)
{
	if (!HAS_DISPLAY(display))
		return;

	intel_display_driver_suspend_access(display);

	/*
	 * Due to the hpd irq storm handling the hotplug work can re-arm the
	 * poll handlers. Hence disable polling after hpd handling is shut down.
	 */
	intel_hpd_poll_fini(display);

	intel_unregister_dsm_handler();

	/* flush any delayed tasks or pending work */
	flush_workqueue(display->wq.unordered);

	intel_hdcp_component_fini(display);

	intel_mode_config_cleanup(display);

	intel_dp_tunnel_mgr_cleanup(display);

	intel_overlay_cleanup(display);

	intel_gmbus_teardown(display);

	destroy_workqueue(display->hotplug.dp_wq);
	destroy_workqueue(display->wq.flip);
	destroy_workqueue(display->wq.modeset);
	destroy_workqueue(display->wq.cleanup);
	destroy_workqueue(display->wq.unordered);

	intel_fbc_cleanup(display);
}

/* part #3: call after gem init */
void intel_display_driver_remove_nogem(struct intel_display *display)
{
	intel_dmc_fini(display);

	intel_power_domains_driver_remove(display);

	intel_vga_unregister(display);

	intel_bios_driver_remove(display);
}

void intel_display_driver_unregister(struct intel_display *display)
{
	if (!HAS_DISPLAY(display))
		return;

	intel_unregister_dsm_handler();

	drm_client_dev_unregister(display->drm);

	/*
	 * After flushing the fbdev (incl. a late async config which
	 * will have delayed queuing of a hotplug event), then flush
	 * the hotplug events.
	 */
	drm_kms_helper_poll_fini(display->drm);

	intel_display_driver_disable_user_access(display);

	intel_audio_deinit(display);

	drm_atomic_helper_shutdown(display->drm);

	acpi_video_unregister();
	intel_opregion_unregister(display);
}

/*
 * turn all crtc's off, but do not adjust state
 * This has to be paired with a call to intel_modeset_setup_hw_state.
 */
int intel_display_driver_suspend(struct intel_display *display)
{
	struct drm_atomic_state *state;
	int ret;

	if (!HAS_DISPLAY(display))
		return 0;

	state = drm_atomic_helper_suspend(display->drm);
	ret = PTR_ERR_OR_ZERO(state);
	if (ret)
		drm_err(display->drm, "Suspending crtc's failed with %i\n",
			ret);
	else
		display->restore.modeset_state = state;

	/* ensure all DPT VMAs have been unpinned for intel_dpt_suspend() */
	flush_workqueue(display->wq.cleanup);

	intel_dp_mst_suspend(display);

	return ret;
}

int
__intel_display_driver_resume(struct intel_display *display,
			      struct drm_atomic_state *state,
			      struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int ret, i;

	intel_modeset_setup_hw_state(display, ctx);

	if (!state)
		return 0;

	/*
	 * We've duplicated the state, pointers to the old state are invalid.
	 *
	 * Don't attempt to use the old state until we commit the duplicated state.
	 */
	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		/*
		 * Force recalculation even if we restore
		 * current state. With fast modeset this may not result
		 * in a modeset when the state is compatible.
		 */
		crtc_state->mode_changed = true;
	}

	/* ignore any reset values/BIOS leftovers in the WM registers */
	if (!HAS_GMCH(display))
		to_intel_atomic_state(state)->skip_intermediate_wm = true;

	ret = drm_atomic_helper_commit_duplicated_state(state, ctx);

	drm_WARN_ON(display->drm, ret == -EDEADLK);

	return ret;
}

void intel_display_driver_resume(struct intel_display *display)
{
	struct drm_atomic_state *state = display->restore.modeset_state;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	if (!HAS_DISPLAY(display))
		return;

	/* MST sideband requires HPD interrupts enabled */
	intel_dp_mst_resume(display);

	display->restore.modeset_state = NULL;
	if (state)
		state->acquire_ctx = &ctx;

	drm_modeset_acquire_init(&ctx, 0);

	while (1) {
		ret = drm_modeset_lock_all_ctx(display->drm, &ctx);
		if (ret != -EDEADLK)
			break;

		drm_modeset_backoff(&ctx);
	}

	if (!ret)
		ret = __intel_display_driver_resume(display, state, &ctx);

	skl_watermark_ipc_update(display);
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	if (ret)
		drm_err(display->drm,
			"Restoring old state failed with %i\n", ret);
	if (state)
		drm_atomic_state_put(state);
}
