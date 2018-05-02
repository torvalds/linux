// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Cobalt NOR flash functions
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/time.h>

#include "cobalt-flash.h"

#define ADRS(offset) (COBALT_BUS_FLASH_BASE + offset)

static struct map_info cobalt_flash_map = {
	.name =		"cobalt-flash",
	.bankwidth =	2,         /* 16 bits */
	.size =		0x4000000, /* 64MB */
	.phys =		0,         /* offset  */
};

static map_word flash_read16(struct map_info *map, unsigned long offset)
{
	map_word r;

	r.x[0] = cobalt_bus_read32(map->virt, ADRS(offset));
	if (offset & 0x2)
		r.x[0] >>= 16;
	else
		r.x[0] &= 0x0000ffff;

	return r;
}

static void flash_write16(struct map_info *map, const map_word datum,
			  unsigned long offset)
{
	u16 data = (u16)datum.x[0];

	cobalt_bus_write16(map->virt, ADRS(offset), data);
}

static void flash_copy_from(struct map_info *map, void *to,
			    unsigned long from, ssize_t len)
{
	u32 src = from;
	u8 *dest = to;
	u32 data;

	while (len) {
		data = cobalt_bus_read32(map->virt, ADRS(src));
		do {
			*dest = data >> (8 * (src & 3));
			src++;
			dest++;
			len--;
		} while (len && (src % 4));
	}
}

static void flash_copy_to(struct map_info *map, unsigned long to,
			  const void *from, ssize_t len)
{
	const u8 *src = from;
	u32 dest = to;

	pr_info("%s: offset 0x%x: length %zu\n", __func__, dest, len);
	while (len) {
		u16 data = 0xffff;

		do {
			data = *src << (8 * (dest & 1));
			src++;
			dest++;
			len--;
		} while (len && (dest % 2));

		cobalt_bus_write16(map->virt, ADRS(dest - 2), data);
	}
}

int cobalt_flash_probe(struct cobalt *cobalt)
{
	struct map_info *map = &cobalt_flash_map;
	struct mtd_info *mtd;

	BUG_ON(!map_bankwidth_supported(map->bankwidth));
	map->virt = cobalt->bar1;
	map->read = flash_read16;
	map->write = flash_write16;
	map->copy_from = flash_copy_from;
	map->copy_to = flash_copy_to;

	mtd = do_map_probe("cfi_probe", map);
	cobalt->mtd = mtd;
	if (!mtd) {
		cobalt_err("Probe CFI flash failed!\n");
		return -1;
	}

	mtd->owner = THIS_MODULE;
	mtd->dev.parent = &cobalt->pci_dev->dev;
	mtd_device_register(mtd, NULL, 0);
	return 0;
}

void cobalt_flash_remove(struct cobalt *cobalt)
{
	if (cobalt->mtd) {
		mtd_device_unregister(cobalt->mtd);
		map_destroy(cobalt->mtd);
	}
}
