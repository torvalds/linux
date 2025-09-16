// SPDX-License-Identifier: MIT

#include <drm/drm_crtc.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_vblank_helper.h>

/**
 * DOC: overview
 *
 * The vblank helper library provides functions for supporting vertical
 * blanking in DRM drivers.
 *
 * For vblank timers, several callback implementations are available.
 * Drivers enable support for vblank timers by setting the vblank callbacks
 * in struct &drm_crtc_funcs to the helpers provided by this library. The
 * initializer macro DRM_CRTC_VBLANK_TIMER_FUNCS does this conveniently.
 *
 * Once the driver enables vblank support with drm_vblank_init(), each
 * CRTC's vblank timer fires according to the programmed display mode. By
 * default, the vblank timer invokes drm_crtc_handle_vblank(). Drivers with
 * more specific requirements can set their own handler function in
 * struct &drm_crtc_helper_funcs.handle_vblank_timeout.
 */

/*
 * VBLANK timer
 */

/**
 * drm_crtc_vblank_helper_enable_vblank_timer - Implements struct &drm_crtc_funcs.enable_vblank
 * @crtc: The CRTC
 *
 * The helper drm_crtc_vblank_helper_enable_vblank_timer() implements
 * enable_vblank of struct drm_crtc_helper_funcs for CRTCs that require
 * a VBLANK timer. It sets up the timer on the first invocation. The
 * started timer expires after the current frame duration. See struct
 * &drm_vblank_crtc.framedur_ns.
 *
 * See also struct &drm_crtc_helper_funcs.enable_vblank.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drm_crtc_vblank_helper_enable_vblank_timer(struct drm_crtc *crtc)
{
	return drm_crtc_vblank_start_timer(crtc);
}
EXPORT_SYMBOL(drm_crtc_vblank_helper_enable_vblank_timer);

/**
 * drm_crtc_vblank_helper_disable_vblank_timer - Implements struct &drm_crtc_funcs.disable_vblank
 * @crtc: The CRTC
 *
 * The helper drm_crtc_vblank_helper_disable_vblank_timer() implements
 * disable_vblank of struct drm_crtc_funcs for CRTCs that require a
 * VBLANK timer.
 *
 * See also struct &drm_crtc_helper_funcs.disable_vblank.
 */
void drm_crtc_vblank_helper_disable_vblank_timer(struct drm_crtc *crtc)
{
	drm_crtc_vblank_cancel_timer(crtc);
}
EXPORT_SYMBOL(drm_crtc_vblank_helper_disable_vblank_timer);

/**
 * drm_crtc_vblank_helper_get_vblank_timestamp_from_timer -
 *	Implements struct &drm_crtc_funcs.get_vblank_timestamp
 * @crtc: The CRTC
 * @max_error: Maximum acceptable error
 * @vblank_time: Returns the next vblank timestamp
 * @in_vblank_irq: True is called from drm_crtc_handle_vblank()
 *
 * The helper drm_crtc_helper_get_vblank_timestamp_from_timer() implements
 * get_vblank_timestamp of struct drm_crtc_funcs for CRTCs that require a
 * VBLANK timer. It returns the timestamp according to the timer's expiry
 * time.
 *
 * See also struct &drm_crtc_funcs.get_vblank_timestamp.
 *
 * Returns:
 * True on success, or false otherwise.
 */
bool drm_crtc_vblank_helper_get_vblank_timestamp_from_timer(struct drm_crtc *crtc,
							    int *max_error,
							    ktime_t *vblank_time,
							    bool in_vblank_irq)
{
	drm_crtc_vblank_get_vblank_timeout(crtc, vblank_time);

	return true;
}
EXPORT_SYMBOL(drm_crtc_vblank_helper_get_vblank_timestamp_from_timer);
