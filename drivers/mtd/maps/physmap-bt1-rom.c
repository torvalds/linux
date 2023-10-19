// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *
 * Baikal-T1 Physically Mapped Internal ROM driver
 */
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/map.h>
#include <linux/mtd/xip.h>
#include <linux/mux/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>

#include "physmap-bt1-rom.h"

/*
 * Baikal-T1 SoC ROMs are only accessible by the dword-aligned instructions.
 * We have to take this into account when implementing the data read-methods.
 * Note there is no need in bothering with endianness, since both Baikal-T1
 * CPU and MMIO are LE.
 */
static map_word __xipram bt1_rom_map_read(struct map_info *map,
					  unsigned long ofs)
{
	void __iomem *src = map->virt + ofs;
	unsigned int shift;
	map_word ret;
	u32 data;

	/* Read data within offset dword. */
	shift = (uintptr_t)src & 0x3;
	data = readl_relaxed(src - shift);
	if (!shift) {
		ret.x[0] = data;
		return ret;
	}
	ret.x[0] = data >> (shift * BITS_PER_BYTE);

	/* Read data from the next dword. */
	shift = 4 - shift;
	if (ofs + shift >= map->size)
		return ret;

	data = readl_relaxed(src + shift);
	ret.x[0] |= data << (shift * BITS_PER_BYTE);

	return ret;
}

static void __xipram bt1_rom_map_copy_from(struct map_info *map,
					   void *to, unsigned long from,
					   ssize_t len)
{
	void __iomem *src = map->virt + from;
	unsigned int shift, chunk;
	u32 data;

	if (len <= 0 || from >= map->size)
		return;

	/* Make sure we don't go over the map limit. */
	len = min_t(ssize_t, map->size - from, len);

	/*
	 * Since requested data size can be pretty big we have to implement
	 * the copy procedure as optimal as possible. That's why it's split
	 * up into the next three stages: unaligned head, aligned body,
	 * unaligned tail.
	 */
	shift = (uintptr_t)src & 0x3;
	if (shift) {
		chunk = min_t(ssize_t, 4 - shift, len);
		data = readl_relaxed(src - shift);
		memcpy(to, (char *)&data + shift, chunk);
		src += chunk;
		to += chunk;
		len -= chunk;
	}

	while (len >= 4) {
		data = readl_relaxed(src);
		memcpy(to, &data, 4);
		src += 4;
		to += 4;
		len -= 4;
	}

	if (len) {
		data = readl_relaxed(src);
		memcpy(to, &data, len);
	}
}

int of_flash_probe_bt1_rom(struct platform_device *pdev,
			   struct device_node *np,
			   struct map_info *map)
{
	struct device *dev = &pdev->dev;

	/* It's supposed to be read-only MTD. */
	if (!of_device_is_compatible(np, "mtd-rom")) {
		dev_info(dev, "No mtd-rom compatible string\n");
		return 0;
	}

	/* Multiplatform guard. */
	if (!of_device_is_compatible(np, "baikal,bt1-int-rom"))
		return 0;

	/* Sanity check the device parameters retrieved from DTB. */
	if (map->bankwidth != 4)
		dev_warn(dev, "Bank width is supposed to be 32 bits wide\n");

	map->read = bt1_rom_map_read;
	map->copy_from = bt1_rom_map_copy_from;

	return 0;
}
