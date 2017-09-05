/*
 *  fbcon_dmi_quirks.c -- DMI based quirk detection for fbcon
 *
 *	Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/dmi.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include "fbcon.h"

/*
 * Some x86 clamshell design devices use portrait tablet screens and a display
 * engine which cannot rotate in hardware, so we need to rotate the fbcon to
 * compensate. Unfortunately these (cheap) devices also typically have quite
 * generic DMI data, so we match on a combination of DMI data, screen resolution
 * and a list of known BIOS dates to avoid false positives.
 */

struct fbcon_dmi_rotate_data {
	int width;
	int height;
	const char * const *bios_dates;
	int rotate;
};

static const struct fbcon_dmi_rotate_data rotate_data_asus_t100ha = {
	.width = 800,
	.height = 1280,
	.rotate = FB_ROTATE_CCW,
};

static const struct fbcon_dmi_rotate_data rotate_data_gpd_pocket = {
	.width = 1200,
	.height = 1920,
	.bios_dates = (const char * const []){ "05/26/2017", "06/28/2017",
		"07/05/2017", "08/07/2017", NULL },
	.rotate = FB_ROTATE_CW,
};

static const struct fbcon_dmi_rotate_data rotate_data_gpd_win = {
	.width = 720,
	.height = 1280,
	.bios_dates = (const char * const []){
		"10/25/2016", "11/18/2016", "02/21/2017",
		"03/20/2017", NULL },
	.rotate = FB_ROTATE_CW,
};

static const struct fbcon_dmi_rotate_data rotate_data_itworks_tw891 = {
	.width = 800,
	.height = 1280,
	.bios_dates = (const char * const []){ "10/16/2015", NULL },
	.rotate = FB_ROTATE_CW,
};

static const struct fbcon_dmi_rotate_data rotate_data_vios_lth17 = {
	.width = 800,
	.height = 1280,
	.rotate = FB_ROTATE_CW,
};

static const struct dmi_system_id rotate_data[] = {
	{	/* Asus T100HA */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T100HAN"),
		},
		.driver_data = (void *)&rotate_data_asus_t100ha,
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
		.driver_data = (void *)&rotate_data_gpd_pocket,
	}, {	/* GPD Win (same note on DMI match as GPD Pocket) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		},
		.driver_data = (void *)&rotate_data_gpd_win,
	}, {	/* I.T.Works TW891 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "To be filled by O.E.M."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "TW891"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "To be filled by O.E.M."),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "TW891"),
		},
		.driver_data = (void *)&rotate_data_itworks_tw891,
	}, {	/* VIOS LTH17 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "VIOS"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LTH17"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "VIOS"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "LTH17"),
		},
		.driver_data = (void *)&rotate_data_vios_lth17,
	},
	{}
};

int fbcon_platform_get_rotate(struct fb_info *info)
{
	const struct dmi_system_id *match;
	const struct fbcon_dmi_rotate_data *data;
	const char *bios_date;
	int i;

	for (match = dmi_first_match(rotate_data);
	     match;
	     match = dmi_first_match(match + 1)) {
		data = match->driver_data;

		if (data->width != info->var.xres ||
		    data->height != info->var.yres)
			continue;

		if (!data->bios_dates)
			return data->rotate;

		bios_date = dmi_get_system_info(DMI_BIOS_DATE);
		if (!bios_date)
			continue;

		for (i = 0; data->bios_dates[i]; i++) {
			if (!strcmp(data->bios_dates[i], bios_date))
				return data->rotate;
		}
	}

	return FB_ROTATE_UR;
}
