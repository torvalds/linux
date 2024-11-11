// SPDX-License-Identifier: GPL-2.0

#include <linux/array_size.h>
#include <linux/dmi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <drm/drm_edid.h>
#include <drm/drm_utils.h>

struct drm_panel_min_backlight_quirk {
	struct {
		enum dmi_field field;
		const char * const value;
	} dmi_match;
	struct drm_edid_ident ident;
	u8 min_brightness;
};

static const struct drm_panel_min_backlight_quirk drm_panel_min_backlight_quirks[] = {
	/* 13 inch matte panel */
	{
		.dmi_match.field = DMI_BOARD_VENDOR,
		.dmi_match.value = "Framework",
		.ident.panel_id = drm_edid_encode_panel_id('B', 'O', 'E', 0x0bca),
		.ident.name = "NE135FBM-N41",
		.min_brightness = 0,
	},
};

static bool drm_panel_min_backlight_quirk_matches(const struct drm_panel_min_backlight_quirk *quirk,
						  const struct drm_edid *edid)
{
	if (!dmi_match(quirk->dmi_match.field, quirk->dmi_match.value))
		return false;

	if (!drm_edid_match(edid, &quirk->ident))
		return false;

	return true;
}

/**
 * drm_get_panel_min_brightness_quirk - Get minimum supported brightness level for a panel.
 * @edid: EDID of the panel to check
 *
 * This function checks for platform specific (e.g. DMI based) quirks
 * providing info on the minimum backlight brightness for systems where this
 * cannot be probed correctly from the hard-/firm-ware.
 *
 * Returns:
 * A negative error value or
 * an override value in the range [0, 255] representing 0-100% to be scaled to
 * the drivers target range.
 */
int drm_get_panel_min_brightness_quirk(const struct drm_edid *edid)
{
	const struct drm_panel_min_backlight_quirk *quirk;
	size_t i;

	if (!IS_ENABLED(CONFIG_DMI))
		return -ENODATA;

	if (!edid)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(drm_panel_min_backlight_quirks); i++) {
		quirk = &drm_panel_min_backlight_quirks[i];

		if (drm_panel_min_backlight_quirk_matches(quirk, edid))
			return quirk->min_brightness;
	}

	return -ENODATA;
}
EXPORT_SYMBOL(drm_get_panel_min_brightness_quirk);

MODULE_DESCRIPTION("Quirks for panel backlight overrides");
MODULE_LICENSE("GPL");
