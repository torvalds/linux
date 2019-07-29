/*
 * Copyright 2018 Hans de Goede <hdegoede@redhat.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/dmi.h>
#include <linux/mod_devicetable.h>
#include "core.h"
#include "common.h"
#include "brcm_hw_ids.h"

/* The DMI data never changes so we can use a static buf for this */
static char dmi_board_type[128];

struct brcmf_dmi_data {
	u32 chip;
	u32 chiprev;
	const char *board_type;
};

/* NOTE: Please keep all entries sorted alphabetically */

static const struct brcmf_dmi_data gpd_win_pocket_data = {
	BRCM_CC_4356_CHIP_ID, 2, "gpd-win-pocket"
};

static const struct brcmf_dmi_data jumper_ezpad_mini3_data = {
	BRCM_CC_43430_CHIP_ID, 0, "jumper-ezpad-mini3"
};

static const struct brcmf_dmi_data meegopad_t08_data = {
	BRCM_CC_43340_CHIP_ID, 2, "meegopad-t08"
};

static const struct dmi_system_id dmi_platform_data[] = {
	{
		/* Match for the GPDwin which unfortunately uses somewhat
		 * generic dmi strings, which is why we test for 4 strings.
		 * Comparing against 23 other byt/cht boards, board_vendor
		 * and board_name are unique to the GPDwin, where as only one
		 * other board has the same board_serial and 3 others have
		 * the same default product_name. Also the GPDwin is the
		 * only device to have both board_ and product_name not set.
		 */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Default string"),
			DMI_MATCH(DMI_BOARD_SERIAL, "Default string"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_win_pocket_data,
	},
	{
		/* Jumper EZpad mini3 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CherryTrail"),
			/* jumperx.T87.KFBNEEA02 with the version-nr dropped */
			DMI_MATCH(DMI_BIOS_VERSION, "jumperx.T87.KFBNEEA"),
		},
		.driver_data = (void *)&jumper_ezpad_mini3_data,
	},
	{
		/* Meegopad T08 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Default string"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
			DMI_MATCH(DMI_BOARD_NAME, "T3 MRD"),
			DMI_MATCH(DMI_BOARD_VERSION, "V1.1"),
		},
		.driver_data = (void *)&meegopad_t08_data,
	},
	{}
};

void brcmf_dmi_probe(struct brcmf_mp_device *settings, u32 chip, u32 chiprev)
{
	const struct dmi_system_id *match;
	const struct brcmf_dmi_data *data;
	const char *sys_vendor;
	const char *product_name;

	/* Some models have DMI strings which are too generic, e.g.
	 * "Default string", we use a quirk table for these.
	 */
	for (match = dmi_first_match(dmi_platform_data);
	     match;
	     match = dmi_first_match(match + 1)) {
		data = match->driver_data;

		if (data->chip == chip && data->chiprev == chiprev) {
			settings->board_type = data->board_type;
			return;
		}
	}

	/* Not found in the quirk-table, use sys_vendor-product_name */
	sys_vendor = dmi_get_system_info(DMI_SYS_VENDOR);
	product_name = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (sys_vendor && product_name) {
		snprintf(dmi_board_type, sizeof(dmi_board_type), "%s-%s",
			 sys_vendor, product_name);
		settings->board_type = dmi_board_type;
	}
}
