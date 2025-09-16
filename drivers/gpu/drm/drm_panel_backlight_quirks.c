// SPDX-License-Identifier: GPL-2.0

#include <linux/array_size.h>
#include <linux/dmi.h>
#include <linux/export.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <drm/drm_edid.h>
#include <drm/drm_utils.h>

struct drm_panel_match {
	enum dmi_field field;
	const char * const value;
};

struct drm_get_panel_backlight_quirk {
	struct drm_panel_match dmi_match;
	struct drm_panel_match dmi_match_other;
	struct drm_edid_ident ident;
	struct drm_panel_backlight_quirk quirk;
};

static const struct drm_get_panel_backlight_quirk drm_panel_min_backlight_quirks[] = {
	/* 13 inch matte panel */
	{
		.dmi_match.field = DMI_BOARD_VENDOR,
		.dmi_match.value = "Framework",
		.ident.panel_id = drm_edid_encode_panel_id('B', 'O', 'E', 0x0bca),
		.ident.name = "NE135FBM-N41",
		.quirk = { .min_brightness = 1, },
	},
	/* 13 inch glossy panel */
	{
		.dmi_match.field = DMI_BOARD_VENDOR,
		.dmi_match.value = "Framework",
		.ident.panel_id = drm_edid_encode_panel_id('B', 'O', 'E', 0x095f),
		.ident.name = "NE135FBM-N41",
		.quirk = { .min_brightness = 1, },
	},
	/* 13 inch 2.8k panel */
	{
		.dmi_match.field = DMI_BOARD_VENDOR,
		.dmi_match.value = "Framework",
		.ident.panel_id = drm_edid_encode_panel_id('B', 'O', 'E', 0x0cb4),
		.ident.name = "NE135A1M-NY1",
		.quirk = { .min_brightness = 1, },
	},
	/* Steam Deck models */
	{
		.dmi_match.field = DMI_SYS_VENDOR,
		.dmi_match.value = "Valve",
		.dmi_match_other.field = DMI_PRODUCT_NAME,
		.dmi_match_other.value = "Jupiter",
		.quirk = { .min_brightness = 1, },
	},
	{
		.dmi_match.field = DMI_SYS_VENDOR,
		.dmi_match.value = "Valve",
		.dmi_match_other.field = DMI_PRODUCT_NAME,
		.dmi_match_other.value = "Galileo",
		.quirk = { .min_brightness = 1, },
	},
	/* Have OLED Panels with brightness issue when last byte is 0/1 */
	{
		.dmi_match.field = DMI_SYS_VENDOR,
		.dmi_match.value = "AYANEO",
		.dmi_match_other.field = DMI_PRODUCT_NAME,
		.dmi_match_other.value = "AYANEO 3",
		.quirk = { .brightness_mask = 3, },
	},
	{
		.dmi_match.field = DMI_SYS_VENDOR,
		.dmi_match.value = "ZOTAC",
		.dmi_match_other.field = DMI_BOARD_NAME,
		.dmi_match_other.value = "G0A1W",
		.quirk = { .brightness_mask = 3, },
	},
	{
		.dmi_match.field = DMI_SYS_VENDOR,
		.dmi_match.value = "ZOTAC",
		.dmi_match_other.field = DMI_BOARD_NAME,
		.dmi_match_other.value = "G1A1W",
		.quirk = { .brightness_mask = 3, },
	},
	{
		.dmi_match.field = DMI_SYS_VENDOR,
		.dmi_match.value = "ONE-NETBOOK",
		.dmi_match_other.field = DMI_PRODUCT_NAME,
		.dmi_match_other.value = "ONEXPLAYER F1Pro",
		.quirk = { .brightness_mask = 3, },
	},
	{
		.dmi_match.field = DMI_SYS_VENDOR,
		.dmi_match.value = "ONE-NETBOOK",
		.dmi_match_other.field = DMI_PRODUCT_NAME,
		.dmi_match_other.value = "ONEXPLAYER F1 EVA-02",
		.quirk = { .brightness_mask = 3, },
	},
};

static bool drm_panel_min_backlight_quirk_matches(
	const struct drm_get_panel_backlight_quirk *quirk,
	const struct drm_edid *edid)
{
	if (quirk->dmi_match.field &&
	    !dmi_match(quirk->dmi_match.field, quirk->dmi_match.value))
		return false;

	if (quirk->dmi_match_other.field &&
	    !dmi_match(quirk->dmi_match_other.field,
		       quirk->dmi_match_other.value))
		return false;

	if (quirk->ident.panel_id && !drm_edid_match(edid, &quirk->ident))
		return false;

	return true;
}

/**
 * drm_get_panel_backlight_quirk - Get backlight quirks for a panel
 * @edid: EDID of the panel to check
 *
 * This function checks for platform specific (e.g. DMI based) quirks
 * providing info on the minimum backlight brightness for systems where this
 * cannot be probed correctly from the hard-/firm-ware and other sources.
 *
 * Returns:
 * a drm_panel_backlight_quirk struct if a quirk was found, otherwise an
 * error pointer.
 */
const struct drm_panel_backlight_quirk *
drm_get_panel_backlight_quirk(const struct drm_edid *edid)
{
	const struct drm_get_panel_backlight_quirk *quirk;
	size_t i;

	if (!IS_ENABLED(CONFIG_DMI))
		return ERR_PTR(-ENODATA);

	if (!edid)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < ARRAY_SIZE(drm_panel_min_backlight_quirks); i++) {
		quirk = &drm_panel_min_backlight_quirks[i];

		if (drm_panel_min_backlight_quirk_matches(quirk, edid))
			return &quirk->quirk;
	}

	return ERR_PTR(-ENODATA);
}
EXPORT_SYMBOL(drm_get_panel_backlight_quirk);

MODULE_DESCRIPTION("Quirks for panel backlight overrides");
MODULE_LICENSE("GPL");
