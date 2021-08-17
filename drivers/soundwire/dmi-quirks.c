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

/*
 * The initial version of the Dell SKU 0A3E did not expose the devices
 * on the correct links.
 */
static const struct adr_remap dell_sku_0A3E[] = {
	/* rt715 on link0 */
	{
		0x00020025d071100,
		0x00021025d071500
	},
	/* rt711 on link1 */
	{
		0x000120025d130800,
		0x000120025d071100,
	},
	/* rt1308 on link2 */
	{
		0x000220025d071500,
		0x000220025d130800
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
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A3E")
		},
		.driver_data = (void *)dell_sku_0A3E,
	},
	{}
};

u64 sdw_dmi_override_adr(struct sdw_bus *bus, u64 addr)
{
	const struct dmi_system_id *dmi_id;

	/* check if any address remap quirk applies */
	dmi_id = dmi_first_match(adr_remap_quirk_table);
	if (dmi_id) {
		struct adr_remap *map;

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
