// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2021 Intel Corporation.

/*
 * Soundwire DMI quirks
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

struct adr_remap {
	u64 adr;
	u64 remapped_adr;
};

/*
 * HP Spectre 360 Convertible devices do not expose the correct _ADR
 * in the DSDT.
 * Remap the bad _ADR values to the ones reported by hardware
 */
static const struct adr_remap hp_spectre_360[] = {
	{
		0x000010025D070100,
		0x000020025D071100
	},
	{
		0x000110025d070100,
		0x000120025D130800
	},
	{}
};

static const struct dmi_system_id adr_remap_quirk_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Spectre x360 Convertible"),
		},
		.driver_data = (void *)hp_spectre_360,
	},
	{}
};

u64 sdw_dmi_override_adr(struct sdw_bus *bus, u64 addr)
{
	const struct dmi_system_id *dmi_id;

	/* check if any address remap quirk applies */
	dmi_id = dmi_first_match(adr_remap_quirk_table);
	if (dmi_id) {
		struct adr_remap *map = dmi_id->driver_data;

		for (map = dmi_id->driver_data; map->adr; map++) {
			if (map->adr == addr) {
				dev_dbg(bus->dev, "remapped _ADR 0x%llx as 0x%llx\n",
					addr, map->remapped_adr);
				addr = map->remapped_adr;
				break;
			}
		}
	}

	return addr;
}
