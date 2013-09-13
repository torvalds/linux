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
#include <bcm47xx_nvram.h>

/* 10 parts were found on sflash on Netgear WNDR4500 */
#define BCM47XXPART_MAX_PARTS		12

/*
 * Amount of bytes we read when analyzing each block of flash memory.
 * Set it big enough to allow detecting partition and reading important data.
 */
#define BCM47XXPART_BYTES_TO_READ	0x404

/* Magics */
#define BOARD_DATA_MAGIC		0x5246504D	/* MPFR */
#define POT_MAGIC1			0x54544f50	/* POTT */
#define POT_MAGIC2			0x504f		/* OP */
#define ML_MAGIC1			0x39685a42
#define ML_MAGIC2			0x26594131
#define TRX_MAGIC			0x30524448

struct trx_header {
	uint32_t magic;
	uint32_t length;
	uint32_t crc32;
	uint16_t flags;
	uint16_t version;
	uint32_t offset[3];
} __packed;

static void bcm47xxpart_add_part(struct mtd_partition *part, char *name,
				 u64 offset, uint32_t mask_flags)
{
	part->name = name;
	part->offset = offset;
	part->mask_flags = mask_flags;
}

static int bcm47xxpart_parse(struct mtd_info *master,
			     struct mtd_partition **pparts,
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

	if (blocksize <= 0x10000)
		blocksize = 0x10000;

	/* Alloc */
	parts = kzalloc(sizeof(struct mtd_partition) * BCM47XXPART_MAX_PARTS,
			GFP_KERNEL);
	buf = kzalloc(BCM47XXPART_BYTES_TO_READ, GFP_KERNEL);

	/* Parse block by block looking for magics */
	for (offset = 0; offset <= master->size - blocksize;
	     offset += blocksize) {
		/* Nothing more in higher memory */
		if (offset >= 0x2000000)
			break;

		if (curr_part > BCM47XXPART_MAX_PARTS) {
			pr_warn("Reached maximum number of partitions, scanning stopped!\n");
			break;
		}

		/* Read beginning of the block */
		if (mtd_read(master, offset, BCM47XXPART_BYTES_TO_READ,
			     &bytes_read, (uint8_t *)buf) < 0) {
			pr_err("mtd_read error while parsing (offset: 0x%X)!\n",
			       offset);
			continue;
		}

		/* CFE has small NVRAM at 0x400 */
		if (buf[0x400 / 4] == NVRAM_HEADER) {
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

			bcm47xxpart_add_part(&parts[curr_part++], "linux",
					     offset + trx->offset[i], 0);
			i++;

			/*
			 * Pure rootfs size is known and can be calculated as:
			 * trx->length - trx->offset[i]. We don't fill it as
			 * we want to have jffs2 (overlay) in the same mtd.
			 */
			bcm47xxpart_add_part(&parts[curr_part++], "rootfs",
					     offset + trx->offset[i], 0);
			i++;

			last_trx_part = curr_part - 1;

			/*
			 * We have whole TRX scanned, skip to the next part. Use
			 * roundown (not roundup), as the loop will increase
			 * offset in next step.
			 */
			offset = rounddown(offset + trx->length, blocksize);
			continue;
		}
	}

	/* Look for NVRAM at the end of the last block. */
	for (i = 0; i < ARRAY_SIZE(possible_nvram_sizes); i++) {
		if (curr_part > BCM47XXPART_MAX_PARTS) {
			pr_warn("Reached maximum number of partitions, scanning stopped!\n");
			break;
		}

		offset = master->size - possible_nvram_sizes[i];
		if (mtd_read(master, offset, 0x4, &bytes_read,
			     (uint8_t *)buf) < 0) {
			pr_err("mtd_read error while reading at offset 0x%X!\n",
			       offset);
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
	.owner = THIS_MODULE,
	.parse_fn = bcm47xxpart_parse,
	.name = "bcm47xxpart",
};

static int __init bcm47xxpart_init(void)
{
	return register_mtd_parser(&bcm47xxpart_mtd_parser);
}

static void __exit bcm47xxpart_exit(void)
{
	deregister_mtd_parser(&bcm47xxpart_mtd_parser);
}

module_init(bcm47xxpart_init);
module_exit(bcm47xxpart_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD partitioning for BCM47XX flash memories");
