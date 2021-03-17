// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright © 2007 Eugene Konev <ejka@openwrt.org>
 *
 * TI AR7 flash partition table.
 * Based on ar7 map by Felix Fietkau <nbd@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/memblock.h>
#include <linux/module.h>

#include <uapi/linux/magic.h>

#define AR7_PARTS	4
#define ROOT_OFFSET	0xe0000

#define LOADER_MAGIC1	le32_to_cpu(0xfeedfa42)
#define LOADER_MAGIC2	le32_to_cpu(0xfeed1281)

struct ar7_bin_rec {
	unsigned int checksum;
	unsigned int length;
	unsigned int address;
};

static int create_mtd_partitions(struct mtd_info *master,
				 const struct mtd_partition **pparts,
				 struct mtd_part_parser_data *data)
{
	struct ar7_bin_rec header;
	unsigned int offset;
	size_t len;
	unsigned int pre_size = master->erasesize, post_size = 0;
	unsigned int root_offset = ROOT_OFFSET;

	int retries = 10;
	struct mtd_partition *ar7_parts;

	ar7_parts = kcalloc(AR7_PARTS, sizeof(*ar7_parts), GFP_KERNEL);
	if (!ar7_parts)
		return -ENOMEM;
	ar7_parts[0].name = "loader";
	ar7_parts[0].offset = 0;
	ar7_parts[0].size = master->erasesize;
	ar7_parts[0].mask_flags = MTD_WRITEABLE;

	ar7_parts[1].name = "config";
	ar7_parts[1].offset = 0;
	ar7_parts[1].size = master->erasesize;
	ar7_parts[1].mask_flags = 0;

	do { /* Try 10 blocks starting from master->erasesize */
		offset = pre_size;
		mtd_read(master, offset, sizeof(header), &len,
			 (uint8_t *)&header);
		if (!strncmp((char *)&header, "TIENV0.8", 8))
			ar7_parts[1].offset = pre_size;
		if (header.checksum == LOADER_MAGIC1)
			break;
		if (header.checksum == LOADER_MAGIC2)
			break;
		pre_size += master->erasesize;
	} while (retries--);

	pre_size = offset;

	if (!ar7_parts[1].offset) {
		ar7_parts[1].offset = master->size - master->erasesize;
		post_size = master->erasesize;
	}

	switch (header.checksum) {
	case LOADER_MAGIC1:
		while (header.length) {
			offset += sizeof(header) + header.length;
			mtd_read(master, offset, sizeof(header), &len,
				 (uint8_t *)&header);
		}
		root_offset = offset + sizeof(header) + 4;
		break;
	case LOADER_MAGIC2:
		while (header.length) {
			offset += sizeof(header) + header.length;
			mtd_read(master, offset, sizeof(header), &len,
				 (uint8_t *)&header);
		}
		root_offset = offset + sizeof(header) + 4 + 0xff;
		root_offset &= ~(uint32_t)0xff;
		break;
	default:
		printk(KERN_WARNING "Unknown magic: %08x\n", header.checksum);
		break;
	}

	mtd_read(master, root_offset, sizeof(header), &len, (u8 *)&header);
	if (header.checksum != SQUASHFS_MAGIC) {
		root_offset += master->erasesize - 1;
		root_offset &= ~(master->erasesize - 1);
	}

	ar7_parts[2].name = "linux";
	ar7_parts[2].offset = pre_size;
	ar7_parts[2].size = master->size - pre_size - post_size;
	ar7_parts[2].mask_flags = 0;

	ar7_parts[3].name = "rootfs";
	ar7_parts[3].offset = root_offset;
	ar7_parts[3].size = master->size - root_offset - post_size;
	ar7_parts[3].mask_flags = 0;

	*pparts = ar7_parts;
	return AR7_PARTS;
}

static struct mtd_part_parser ar7_parser = {
	.parse_fn = create_mtd_partitions,
	.name = "ar7part",
};
module_mtd_part_parser(ar7_parser);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(	"Felix Fietkau <nbd@openwrt.org>, "
		"Eugene Konev <ejka@openwrt.org>");
MODULE_DESCRIPTION("MTD partitioning for TI AR7");
