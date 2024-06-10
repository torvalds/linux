// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Platform Management Framework Driver Quirks
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#include <linux/dmi.h>

#include "pmf.h"

struct quirk_entry {
	u32 supported_func;
};

static struct quirk_entry quirk_no_sps_bug = {
	.supported_func = 0x4003,
};

static const struct dmi_system_id fwbug_list[] = {
	{
		.ident = "ROG Zephyrus G14",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "GA403UV"),
		},
		.driver_data = &quirk_no_sps_bug,
	},
	{}
};

void amd_pmf_quirks_init(struct amd_pmf_dev *dev)
{
	const struct dmi_system_id *dmi_id;
	struct quirk_entry *quirks;

	dmi_id = dmi_first_match(fwbug_list);
	if (!dmi_id)
		return;

	quirks = dmi_id->driver_data;
	if (quirks->supported_func) {
		dev->supported_func = quirks->supported_func;
		pr_info("Using supported funcs quirk to avoid %s platform firmware bug\n",
			dmi_id->ident);
	}
}

