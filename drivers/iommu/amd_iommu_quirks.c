/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Quirks for AMD IOMMU
 *
 * Copyright (C) 2019 Kai-Heng Feng <kai.heng.feng@canonical.com>
 */

#ifdef CONFIG_DMI
#include <linux/dmi.h>

#include "amd_iommu.h"

#define IVHD_SPECIAL_IOAPIC		1

struct ivrs_quirk_entry {
	u8 id;
	u16 devid;
};

enum {
	DELL_INSPIRON_7375 = 0,
	DELL_LATITUDE_5495,
	LENOVO_IDEAPAD_330S_15ARR,
};

static const struct ivrs_quirk_entry ivrs_ioapic_quirks[][3] __initconst = {
	/* ivrs_ioapic[4]=00:14.0 ivrs_ioapic[5]=00:00.2 */
	[DELL_INSPIRON_7375] = {
		{ .id = 4, .devid = 0xa0 },
		{ .id = 5, .devid = 0x2 },
		{}
	},
	/* ivrs_ioapic[4]=00:14.0 */
	[DELL_LATITUDE_5495] = {
		{ .id = 4, .devid = 0xa0 },
		{}
	},
	/* ivrs_ioapic[32]=00:14.0 */
	[LENOVO_IDEAPAD_330S_15ARR] = {
		{ .id = 32, .devid = 0xa0 },
		{}
	},
	{}
};

static int __init ivrs_ioapic_quirk_cb(const struct dmi_system_id *d)
{
	const struct ivrs_quirk_entry *i;

	for (i = d->driver_data; i->id != 0 && i->devid != 0; i++)
		add_special_device(IVHD_SPECIAL_IOAPIC, i->id, (u16 *)&i->devid, 0);

	return 0;
}

static const struct dmi_system_id ivrs_quirks[] __initconst = {
	{
		.callback = ivrs_ioapic_quirk_cb,
		.ident = "Dell Inspiron 7375",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 7375"),
		},
		.driver_data = (void *)&ivrs_ioapic_quirks[DELL_INSPIRON_7375],
	},
	{
		.callback = ivrs_ioapic_quirk_cb,
		.ident = "Dell Latitude 5495",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude 5495"),
		},
		.driver_data = (void *)&ivrs_ioapic_quirks[DELL_LATITUDE_5495],
	},
	{
		.callback = ivrs_ioapic_quirk_cb,
		.ident = "Lenovo ideapad 330S-15ARR",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "81FB"),
		},
		.driver_data = (void *)&ivrs_ioapic_quirks[LENOVO_IDEAPAD_330S_15ARR],
	},
	{}
};

void __init amd_iommu_apply_ivrs_quirks(void)
{
	dmi_check_system(ivrs_quirks);
}
#endif
