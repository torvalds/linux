// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _INTEL_DISPLAY_PARAMS_H_
#define _INTEL_DISPLAY_PARAMS_H_

#include <linux/types.h>

struct drm_printer;

/*
 * Invoke param, a function-like macro, for each intel display param, with
 * arguments:
 *
 * param(type, name, value, mode)
 *
 * type: parameter type, one of {bool, int, unsigned int, unsigned long, char *}
 * name: name of the parameter
 * value: initial/default value of the parameter
 * mode: debugfs file permissions, one of {0400, 0600, 0}, use 0 to not create
 *       debugfs file
 */
#define INTEL_DISPLAY_PARAMS_FOR_EACH(param) \
	param(char *, dmc_firmware_path, NULL, 0400) \
	param(char *, vbt_firmware, NULL, 0400) \
	param(int, lvds_channel_mode, 0, 0400) \
	param(int, panel_use_ssc, -1, 0600) \
	param(int, vbt_sdvo_panel_type, -1, 0400) \
	param(int, enable_dc, -1, 0400) \
	param(bool, enable_dpt, true, 0400) \
	param(bool, enable_dsb, true, 0600) \
	param(bool, enable_sagv, true, 0600) \
	param(int, disable_power_well, -1, 0400) \
	param(bool, enable_ips, true, 0600) \
	param(int, invert_brightness, 0, 0600) \
	param(int, edp_vswing, 0, 0400) \
	param(int, enable_dpcd_backlight, -1, 0600) \
	param(bool, load_detect_test, false, 0600) \
	param(bool, force_reset_modeset_test, false, 0600) \
	param(bool, disable_display, false, 0400) \
	param(bool, verbose_state_checks, true, 0400) \
	param(bool, nuclear_pageflip, false, 0400) \
	param(bool, enable_dp_mst, true, 0600) \
	param(int, enable_fbc, -1, 0600) \
	param(int, enable_psr, -1, 0600) \
	param(bool, psr_safest_params, false, 0400) \
	param(bool, enable_psr2_sel_fetch, true, 0400) \
	param(bool, enable_dmc_wl, false, 0400) \

#define MEMBER(T, member, ...) T member;
struct intel_display_params {
	INTEL_DISPLAY_PARAMS_FOR_EACH(MEMBER);
};
#undef MEMBER

void intel_display_params_dump(const struct intel_display_params *params,
			       const char *driver_name, struct drm_printer *p);
void intel_display_params_copy(struct intel_display_params *dest);
void intel_display_params_free(struct intel_display_params *params);

#endif
