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
 * Some TigerLake devices based on an initial Intel BIOS do not expose
 * the correct _ADR in the DSDT.
 * Remap the bad _ADR values to the ones reported by hardware
 */
static const struct adr_remap intel_tgl_bios[] = {
	{
		0x000010025D070100ull,
		0x000020025D071100ull
	},
	{
		0x000110025d070100ull,
		0x000120025D130800ull
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
		0x00020025d071100ull,
		0x00021025d071500ull
	},
	/* rt711 on link1 */
	{
		0x000120025d130800ull,
		0x000120025d071100ull,
	},
	/* rt1308 on link2 */
	{
		0x000220025d071500ull,
		0x000220025d130800ull
	},
	{}
};

/*
 * The HP Omen 16-k0005TX does not expose the correct version of RT711 on link0
 * and does not expose a RT1316 on link3
 */
static const struct adr_remap hp_omen_16[] = {
	/* rt711-sdca on link0 */
	{
		0x000020025d071100ull,
		0x000030025d071101ull
	},
	/* rt1316-sdca on link3 */
	{
		0x000120025d071100ull,
		0x000330025d131601ull
	},
	{}
};

/*
 * Intel NUC M15 LAPRC510 and LAPRC710
 */
static const struct adr_remap intel_rooks_county[] = {
	/* rt711-sdca on link0 */
	{
		0x000020025d071100ull,
		0x000030025d071101ull
	},
	/* rt1316-sdca on link2 */
	{
		0x000120025d071100ull,
		0x000230025d131601ull
	},
	{}
};

static const struct dmi_system_id adr_remap_quirk_table[] = {
	/* TGL devices */
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Spectre x360 Conv"),
		},
		.driver_data = (void *)intel_tgl_bios,
	},
	{
		/* quirk used for NUC15 'Bishop County' LAPBC510 and LAPBC710 skews */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LAPBC"),
		},
		.driver_data = (void *)intel_tgl_bios,
	},
	{
		/* quirk used for NUC15 LAPBC710 skew */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "LAPBC710"),
		},
		.driver_data = (void *)intel_tgl_bios,
	},
	{
		/* quirk used for NUC15 'Rooks County' LAPRC510 and LAPRC710 skews */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LAPRC"),
		},
		.driver_data = (void *)intel_rooks_county,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A3E")
		},
		.driver_data = (void *)dell_sku_0A3E,
	},
	/* ADL devices */
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "OMEN by HP Gaming Laptop 16-k0xxx"),
		},
		.driver_data = (void *)hp_omen_16,
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
