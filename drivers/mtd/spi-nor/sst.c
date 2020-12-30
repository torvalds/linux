// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info sst_parts[] = {
	/* SST -- large erase sizes are "overlays", "sectors" are 4K */
	{ "sst25vf040b", INFO(0xbf258d, 0, 64 * 1024,  8,
			      SECT_4K | SST_WRITE) },
	{ "sst25vf080b", INFO(0xbf258e, 0, 64 * 1024, 16,
			      SECT_4K | SST_WRITE) },
	{ "sst25vf016b", INFO(0xbf2541, 0, 64 * 1024, 32,
			      SECT_4K | SST_WRITE) },
	{ "sst25vf032b", INFO(0xbf254a, 0, 64 * 1024, 64,
			      SECT_4K | SST_WRITE) },
	{ "sst25vf064c", INFO(0xbf254b, 0, 64 * 1024, 128,
			      SECT_4K | SPI_NOR_4BIT_BP) },
	{ "sst25wf512",  INFO(0xbf2501, 0, 64 * 1024,  1,
			      SECT_4K | SST_WRITE) },
	{ "sst25wf010",  INFO(0xbf2502, 0, 64 * 1024,  2,
			      SECT_4K | SST_WRITE) },
	{ "sst25wf020",  INFO(0xbf2503, 0, 64 * 1024,  4,
			      SECT_4K | SST_WRITE) },
	{ "sst25wf020a", INFO(0x621612, 0, 64 * 1024,  4, SECT_4K) },
	{ "sst25wf040b", INFO(0x621613, 0, 64 * 1024,  8, SECT_4K) },
	{ "sst25wf040",  INFO(0xbf2504, 0, 64 * 1024,  8,
			      SECT_4K | SST_WRITE) },
	{ "sst25wf080",  INFO(0xbf2505, 0, 64 * 1024, 16,
			      SECT_4K | SST_WRITE) },
	{ "sst26wf016b", INFO(0xbf2651, 0, 64 * 1024, 32,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "sst26vf016b", INFO(0xbf2641, 0, 64 * 1024, 32,
			      SECT_4K | SPI_NOR_DUAL_READ) },
	{ "sst26vf064b", INFO(0xbf2643, 0, 64 * 1024, 128,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
};

static int sst_write(struct mtd_info *mtd, loff_t to, size_t len,
		     size_t *retlen, const u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	size_t actual = 0;
	int ret;

	dev_dbg(nor->dev, "to 0x%08x, len %zd\n", (u32)to, len);

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		goto out;

	nor->sst_write_second = false;

	/* Start write from odd address. */
	if (to % 2) {
		nor->program_opcode = SPINOR_OP_BP;

		/* write one byte. */
		ret = spi_nor_write_data(nor, to, 1, buf);
		if (ret < 0)
			goto out;
		WARN(ret != 1, "While writing 1 byte written %i bytes\n", ret);
		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto out;

		to++;
		actual++;
	}

	/* Write out most of the data here. */
	for (; actual < len - 1; actual += 2) {
		nor->program_opcode = SPINOR_OP_AAI_WP;

		/* write two bytes. */
		ret = spi_nor_write_data(nor, to, 2, buf + actual);
		if (ret < 0)
			goto out;
		WARN(ret != 2, "While writing 2 bytes written %i bytes\n", ret);
		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto out;
		to += 2;
		nor->sst_write_second = true;
	}
	nor->sst_write_second = false;

	ret = spi_nor_write_disable(nor);
	if (ret)
		goto out;

	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		goto out;

	/* Write out trailing byte if it exists. */
	if (actual != len) {
		ret = spi_nor_write_enable(nor);
		if (ret)
			goto out;

		nor->program_opcode = SPINOR_OP_BP;
		ret = spi_nor_write_data(nor, to, 1, buf + actual);
		if (ret < 0)
			goto out;
		WARN(ret != 1, "While writing 1 byte written %i bytes\n", ret);
		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto out;

		actual += 1;

		ret = spi_nor_write_disable(nor);
	}
out:
	*retlen += actual;
	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static void sst_default_init(struct spi_nor *nor)
{
	nor->flags |= SNOR_F_HAS_LOCK;
}

static void sst_post_sfdp_fixups(struct spi_nor *nor)
{
	if (nor->info->flags & SST_WRITE)
		nor->mtd._write = sst_write;
}

static const struct spi_nor_fixups sst_fixups = {
	.default_init = sst_default_init,
	.post_sfdp = sst_post_sfdp_fixups,
};

const struct spi_nor_manufacturer spi_nor_sst = {
	.name = "sst",
	.parts = sst_parts,
	.nparts = ARRAY_SIZE(sst_parts),
	.fixups = &sst_fixups,
};
