/*
 * BCM47XX MTD partitioning
 *
 * Copyright © 2012 Rafał Miłecki <zajec5@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <uapi/linux/magic.h>

/*
 * NAND flash on Netgear R6250 was verified to contain 15 partitions.
 * This will result in allocating too big array for some old devices, but the
 * memory will be freed soon anyway (see mtd_device_parse_register).
 */
#define BCM47XXPART_MAX_PARTS		20

/*
 * Amount of bytes we read when analyzing each block of flash memory.
 * Set it big enough to allow detecting partition and reading important data.
 */
#define BCM47XXPART_BYTES_TO_READ	0x4e8

/* Magics */
#define BOARD_DATA_MAGIC		0x5246504D	/* MPFR */
#define BOARD_DATA_MAGIC2		0xBD0D0BBD
#define CFE_MAGIC			0x43464531	/* 1EFC */
#define FACTORY_MAGIC			0x59544346	/* FCTY */
#define NVRAM_HEADER			0x48534C46	/* FLSH */
#define POT_MAGIC1			0x54544f50	/* POTT */
#define POT_MAGIC2			0x504f		/* OP */
#define ML_MAGIC1			0x39685a42
#define ML_MAGIC2			0x26594131
#define TRX_MAGIC			0x30524448
#define SHSQ_MAGIC			0x71736873	/* shsq (weird ZTE H218N endianness) */
#define UBI_EC_MAGIC			0x23494255	/* UBI# */

struct trx_header {
	uint32_t magic;
	uint32_t length;
	uint32_t crc32;
	uint16_t flags;
	uint16_t version;
	uint32_t offset[3];
} __packed;

static void bcm47xxpart_add_part(struct mtd_partition *part, const char *name,
				 u64 offset, uint32_t mask_flags)
{
	part->name = name;
	part->offset = offset;
	part->mask_flags = mask_flags;
}

static const char *bcm47xxpart_trx_data_part_name(struct mtd_info *master,
						  size_t offset)
{
	uint32_t buf;
	size_t bytes_read;
	int err;

	err  = mtd_read(master, offset, sizeof(buf), &bytes_read,
			(uint8_t *)&buf);
	if (err && !mtd_is_bitflip(err)) {
		pr_err("mtd_read error while parsing (offset: 0x%X): %d\n",
			offset, err);
		goto out_default;
	}

	if (buf == UBI_EC_MAGIC)
		return "ubi";

out_default:
	return "rootfs";
}

static int bcm47xxpart_parse(struct mtd_info *master,
			     const struct mtd_partition **pparts,
			     struct mtd_part_parser_data *data)
{
	struct mtd_partition *parts;
	uint8_t i, curr_part = 0;
	uint32_t *buf;
	size_t bytes_read;
	uint32_t offset;
	uint32_t blocksize = master->erasesize;
	struct trx_header *trx;
	int trx_part = -1;
	int last_trx_part = -1;
	int possible_nvram_sizes[] = { 0x8000, 0xF000, 0x10000, };
	int err;

	/*
	 * Some really old flashes (like AT45DB*) had smaller erasesize-s, but
	 * partitions were aligned to at least 0x1000 anyway.
	 */
	if (blocksize < 0x1000)
		blocksize = 0x1000;

	/* Alloc */
	parts = kzalloc(sizeof(struct mtd_partition) * BCM47XXPART_MAX_PARTS,
			GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	buf = kzalloc(BCM47XXPART_BYTES_TO_READ, GFP_KERNEL);
	if (!buf) {
		kfree(parts);
		return -ENOMEM;
	}

	/* Parse block by block looking for magics */
	for (offset = 0; offset <= master->size - blocksize;
	     offset += blocksize) {
		/* Nothing more in higher memory on BCM47XX (MIPS) */
		if (IS_ENABLED(CONFIG_BCM47XX) && offset >= 0x2000000)
			break;

		if (curr_part >= BCM47XXPART_MAX_PARTS) {
			pr_warn("Reached maximum number of partitions, scanning stopped!\n");
			break;
		}

		/* Read beginning of the block */
		err = mtd_read(master, offset, BCM47XXPART_BYTES_TO_READ,
			       &bytes_read, (uint8_t *)buf);
		if (err && !mtd_is_bitflip(err)) {
			pr_err("mtd_read error while parsing (offset: 0x%X): %d\n",
			       offset, err);
			continue;
		}

		/* Magic or small NVRAM at 0x400 */
		if ((buf[0x4e0 / 4] == CFE_MAGIC && buf[0x4e4 / 4] == CFE_MAGIC) ||
		    (buf[0x400 / 4] == NVRAM_HEADER)) {
			bcm47xxpart_add_part(&parts[curr_part++], "boot",
					     offset, MTD_WRITEABLE);
			continue;
		}

		/*
		 * board_data starts with board_id which differs across boards,
		 * but we can use 'MPFR' (hopefully) magic at 0x100
		 */
		if (buf[0x100 / 4] == BOARD_DATA_MAGIC) {
			bcm47xxpart_add_part(&parts[curr_part++], "board_data",
					     offset, MTD_WRITEABLE);
			continue;
		}

		/* Found on Huawei E970 */
		if (buf[0x000 / 4] == FACTORY_MAGIC) {
			bcm47xxpart_add_part(&parts[curr_part++], "factory",
					     offset, MTD_WRITEABLE);
			continue;
		}

		/* POT(TOP) */
		if (buf[0x000 / 4] == POT_MAGIC1 &&
		    (buf[0x004 / 4] & 0xFFFF) == POT_MAGIC2) {
			bcm47xxpart_add_part(&parts[curr_part++], "POT", offset,
					     MTD_WRITEABLE);
			continue;
		}

		/* ML */
		if (buf[0x010 / 4] == ML_MAGIC1 &&
		    buf[0x014 / 4] == ML_MAGIC2) {
			bcm47xxpart_add_part(&parts[curr_part++], "ML", offset,
					     MTD_WRITEABLE);
			continue;
		}

		/* TRX */
		if (buf[0x000 / 4] == TRX_MAGIC) {
			if (BCM47XXPART_MAX_PARTS - curr_part < 4) {
				pr_warn("Not enough partitions left to register trx, scanning stopped!\n");
				break;
			}

			trx = (struct trx_header *)buf;

			trx_part = curr_part;
			bcm47xxpart_add_part(&parts[curr_part++], "firmware",
					     offset, 0);

			i = 0;
			/* We have LZMA loader if offset[2] points to sth */
			if (trx->offset[2]) {
				bcm47xxpart_add_part(&parts[curr_part++],
						     "loader",
						     offset + trx->offset[i],
						     0);
				i++;
			}

			if (trx->offset[i]) {
				bcm47xxpart_add_part(&parts[curr_part++],
						     "linux",
						     offset + trx->offset[i],
						     0);
				i++;
			}

			/*
			 * Pure rootfs size is known and can be calculated as:
			 * trx->length - trx->offset[i]. We don't fill it as
			 * we want to have jffs2 (overlay) in the same mtd.
			 */
			if (trx->offset[i]) {
				const char *name;

				name = bcm47xxpart_trx_data_part_name(master, offset + trx->offset[i]);
				bcm47xxpart_add_part(&parts[curr_part++],
						     name,
						     offset + trx->offset[i],
						     0);
				i++;
			}

			last_trx_part = curr_part - 1;

			/*
			 * We have whole TRX scanned, skip to the next part. Use
			 * roundown (not roundup), as the loop will increase
			 * offset in next step.
			 */
			offset = rounddown(offset + trx->length, blocksize);
			continue;
		}

		/* Squashfs on devices not using TRX */
		if (le32_to_cpu(buf[0x000 / 4]) == SQUASHFS_MAGIC ||
		    buf[0x000 / 4] == SHSQ_MAGIC) {
			bcm47xxpart_add_part(&parts[curr_part++], "rootfs",
					     offset, 0);
			continue;
		}

		/*
		 * New (ARM?) devices may have NVRAM in some middle block. Last
		 * block will be checked later, so skip it.
		 */
		if (offset != master->size - blocksize &&
		    buf[0x000 / 4] == NVRAM_HEADER) {
			bcm47xxpart_add_part(&parts[curr_part++], "nvram",
					     offset, 0);
			continue;
		}

		/* Read middle of the block */
		err = mtd_read(master, offset + 0x8000, 0x4, &bytes_read,
			       (uint8_t *)buf);
		if (err && !mtd_is_bitflip(err)) {
			pr_err("mtd_read error while parsing (offset: 0x%X): %d\n",
			       offset, err);
			continue;
		}

		/* Some devices (ex. WNDR3700v3) don't have a standard 'MPFR' */
		if (buf[0x000 / 4] == BOARD_DATA_MAGIC2) {
			bcm47xxpart_add_part(&parts[curr_part++], "board_data",
					     offset, MTD_WRITEABLE);
			continue;
		}
	}

	/* Look for NVRAM at the end of the last block. */
	for (i = 0; i < ARRAY_SIZE(possible_nvram_sizes); i++) {
		if (curr_part >= BCM47XXPART_MAX_PARTS) {
			pr_warn("Reached maximum number of partitions, scanning stopped!\n");
			break;
		}

		offset = master->size - possible_nvram_sizes[i];
		err = mtd_read(master, offset, 0x4, &bytes_read,
			       (uint8_t *)buf);
		if (err && !mtd_is_bitflip(err)) {
			pr_err("mtd_read error while reading (offset 0x%X): %d\n",
			       offset, err);
			continue;
		}

		/* Standard NVRAM */
		if (buf[0] == NVRAM_HEADER) {
			bcm47xxpart_add_part(&parts[curr_part++], "nvram",
					     master->size - blocksize, 0);
			break;
		}
	}

	kfree(buf);

	/*
	 * Assume that partitions end at the beginning of the one they are
	 * followed by.
	 */
	for (i = 0; i < curr_part; i++) {
		u64 next_part_offset = (i < curr_part - 1) ?
				       parts[i + 1].offset : master->size;

		parts[i].size = next_part_offset - parts[i].offset;
		if (i == last_trx_part && trx_part >= 0)
			parts[trx_part].size = next_part_offset -
					       parts[trx_part].offset;
	}

	*pparts = parts;
	return curr_part;
};

static struct mtd_part_parser bcm47xxpart_mtd_parser = {
	.parse_fn = bcm47xxpart_parse,
	.name = "bcm47xxpart",
};
module_mtd_part_parser(bcm47xxpart_mtd_parser);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD partitioning for BCM47XX flash memories");
