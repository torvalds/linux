/* SPDX-License-Identifier: MIT */
/*
 * drm_panel_orientation_quirks.c -- Quirks for non-normal panel orientation
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Note the quirks in this file are shared with fbdev/efifb and as such
 * must not depend on other drm code.
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <drm/drm_connector.h>
#include <drm/drm_utils.h>

#ifdef CONFIG_DMI

/*
 * Some x86 clamshell design devices use portrait tablet screens and a display
 * engine which cannot rotate in hardware, so we need to rotate the fbcon to
 * compensate. Unfortunately these (cheap) devices also typically have quite
 * generic DMI data, so we match on a combination of DMI data, screen resolution
 * and a list of known BIOS dates to avoid false positives.
 */

struct drm_dmi_panel_orientation_data {
	int width;
	int height;
	const char * const *bios_dates;
	int orientation;
};

static const struct drm_dmi_panel_orientation_data asus_t100ha = {
	.width = 800,
	.height = 1280,
	.orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
};

static const struct drm_dmi_panel_orientation_data gpd_pocket = {
	.width = 1200,
	.height = 1920,
	.bios_dates = (const char * const []){ "05/26/2017", "06/28/2017",
		"07/05/2017", "08/07/2017", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data gpd_win = {
	.width = 720,
	.height = 1280,
	.bios_dates = (const char * const []){
		"10/25/2016", "11/18/2016", "12/23/2016", "12/26/2016",
		"02/21/2017", "03/20/2017", "05/25/2017", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data itworks_tw891 = {
	.width = 800,
	.height = 1280,
	.bios_dates = (const char * const []){ "10/16/2015", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd800x1280_rightside_up = {
	.width = 800,
	.height = 1280,
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct dmi_system_id orientation_data[] = {
	{	/* Asus T100HA */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T100HAN"),
		},
		.driver_data = (void *)&asus_t100ha,
	}, {	/*
		 * GPD Pocket, note that the the DMI data is less generic then
		 * it seems, devices with a board-vendor of "AMI Corporation"
		 * are quite rare, as are devices which have both board- *and*
		 * product-id set to "Default String"
		 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_pocket,
	}, {	/* GPD Win (same note on DMI match as GPD Pocket) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_win,
	}, {	/* I.T.Works TW891 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "To be filled by O.E.M."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "TW891"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "To be filled by O.E.M."),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "TW891"),
		},
		.driver_data = (void *)&itworks_tw891,
	}, {	/*
		 * Lenovo Ideapad Miix 310 laptop, only some production batches
		 * have a portrait screen, the resolution checks makes the quirk
		 * apply only to those batches.
		 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "80SG"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "MIIX 310-10ICR"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Lenovo Ideapad Miix 320 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "80XF"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "Lenovo MIIX 320-10ICR"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* VIOS LTH17 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "VIOS"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LTH17"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	},
	{}
};

/**
 * drm_get_panel_orientation_quirk - Check for panel orientation quirks
 * @width: width in pixels of the panel
 * @height: height in pixels of the panel
 *
 * This function checks for platform specific (e.g. DMI based) quirks
 * providing info on panel_orientation for systems where this cannot be
 * probed from the hard-/firm-ware. To avoid false-positive this function
 * takes the panel resolution as argument and checks that against the
 * resolution expected by the quirk-table entry.
 *
 * Note this function is also used outside of the drm-subsys, by for example
 * the efifb code. Because of this this function gets compiled into its own
 * kernel-module when built as a module.
 *
 * Returns:
 * A DRM_MODE_PANEL_ORIENTATION_* value if there is a quirk for this system,
 * or DRM_MODE_PANEL_ORIENTATION_UNKNOWN if there is no quirk.
 */
int drm_get_panel_orientation_quirk(int width, int height)
{
	const struct dmi_system_id *match;
	const struct drm_dmi_panel_orientation_data *data;
	const char *bios_date;
	int i;

	for (match = dmi_first_match(orientation_data);
	     match;
	     match = dmi_first_match(match + 1)) {
		data = match->driver_data;

		if (data->width != width ||
		    data->height != height)
			continue;

		if (!data->bios_dates)
			return data->orientation;

		bios_date = dmi_get_system_info(DMI_BIOS_DATE);
		if (!bios_date)
			continue;

		i = match_string(data->bios_dates, -1, bios_date);
		if (i >= 0)
			return data->orientation;
	}

	return DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
}
EXPORT_SYMBOL(drm_get_panel_orientation_quirk);

#else

/* There are no quirks for non x86 devices yet */
int drm_get_panel_orientation_quirk(int width, int height)
{
	return DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
}
EXPORT_SYMBOL(drm_get_panel_orientation_quirk);

#endif

MODULE_LICENSE("Dual MIT/GPL");
