// SPDX-License-Identifier: GPL-2.0-only
/*
 * Out-of-line map I/O functions for simple maps when CONFIG_COMPLEX_MAPPINGS
 * is enabled.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/mtd/map.h>
#include <linux/mtd/xip.h>

static map_word __xipram simple_map_read(struct map_info *map, unsigned long ofs)
{
	return inline_map_read(map, ofs);
}

static void __xipram simple_map_write(struct map_info *map, const map_word datum, unsigned long ofs)
{
	inline_map_write(map, datum, ofs);
}

static void __xipram simple_map_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	inline_map_copy_from(map, to, from, len);
}

static void __xipram simple_map_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	inline_map_copy_to(map, to, from, len);
}

void simple_map_init(struct map_info *map)
{
	BUG_ON(!map_bankwidth_supported(map->bankwidth));

	map->read = simple_map_read;
	map->write = simple_map_write;
	map->copy_from = simple_map_copy_from;
	map->copy_to = simple_map_copy_to;
}

EXPORT_SYMBOL(simple_map_init);
MODULE_DESCRIPTION("Out-of-line map I/O");
MODULE_LICENSE("GPL");
