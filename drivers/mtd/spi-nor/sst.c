// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

/* SST flash_info mfr_flag. Used to specify SST byte programming. */
#define SST_WRITE		BIT(0)

#define SST26VF_CR_BPNV		BIT(3)

static int sst26vf_nor_lock(struct spi_nor *nor, loff_t ofs, u64 len)
{
	return -EOPNOTSUPP;
}

static int sst26vf_nor_unlock(struct spi_nor *nor, loff_t ofs, u64 len)
{
	int ret;

	/* We only support unlocking the entire flash array. */
	if (ofs != 0 || len != nor->params->size)
		return -EINVAL;

	ret = spi_nor_read_cr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	if (!(nor->bouncebuf[0] & SST26VF_CR_BPNV)) {
		dev_dbg(nor->dev, "Any block has been permanently locked\n");
		return -EINVAL;
	}

	return spi_nor_global_block_unlock(nor);
}

static int sst26vf_nor_is_locked(struct spi_nor *nor, loff_t ofs, u64 len)
{
	return -EOPNOTSUPP;
}

static const struct spi_nor_locking_ops sst26vf_nor_locking_ops = {
	.lock = sst26vf_nor_lock,
	.unlock = sst26vf_nor_unlock,
	.is_locked = sst26vf_nor_is_locked,
};

static int sst26vf_nor_late_init(struct spi_nor *nor)
{
	nor->params->locking_ops = &sst26vf_nor_locking_ops;

	return 0;
}

static const struct spi_nor_fixups sst26vf_nor_fixups = {
	.late_init = sst26vf_nor_late_init,
};

static const struct flash_info sst_nor_parts[] = {
	{
		.id = SNOR_ID(0x62, 0x16, 0x12),
		.name = "sst25wf020a",
		.size = SZ_256K,
		.flags = SPI_NOR_HAS_LOCK,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x62, 0x16, 0x13),
		.name = "sst25wf040b",
		.size = SZ_512K,
		.flags = SPI_NOR_HAS_LOCK,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x01),
		.name = "sst25wf512",
		.size = SZ_64K,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x02),
		.name = "sst25wf010",
		.size = SZ_128K,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x03),
		.name = "sst25wf020",
		.size = SZ_256K,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x04),
		.name = "sst25wf040",
		.size = SZ_512K,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x05),
		.name = "sst25wf080",
		.size = SZ_1M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x41),
		.name = "sst25vf016b",
		.size = SZ_2M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x4a),
		.name = "sst25vf032b",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x4b),
		.name = "sst25vf064c",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_4BIT_BP | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x8d),
		.name = "sst25vf040b",
		.size = SZ_512K,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x25, 0x8e),
		.name = "sst25vf080b",
		.size = SZ_1M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SNOR_ID(0xbf, 0x26, 0x41),
		.name = "sst26vf016b",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ,
	}, {
		.id = SNOR_ID(0xbf, 0x26, 0x42),
		.name = "sst26vf032b",
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.fixups = &sst26vf_nor_fixups,
	}, {
		.id = SNOR_ID(0xbf, 0x26, 0x43),
		.name = "sst26vf064b",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixups = &sst26vf_nor_fixups,
	}, {
		.id = SNOR_ID(0xbf, 0x26, 0x51),
		.name = "sst26wf016b",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}
};

static int sst_nor_write_data(struct spi_nor *nor, loff_t to, size_t len,
			      const u_char *buf)
{
	u8 op = (len == 1) ? SPINOR_OP_BP : SPINOR_OP_AAI_WP;
	int ret;

	nor->program_opcode = op;
	ret = spi_nor_write_data(nor, to, 1, buf);
	if (ret < 0)
		return ret;
	WARN(ret != len, "While writing %zu byte written %i bytes\n", len, ret);

	return spi_nor_wait_till_ready(nor);
}

static int sst_nor_write(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	size_t actual = 0;
	int ret;

	dev_dbg(nor->dev, "to 0x%08x, len %zd\n", (u32)to, len);

	ret = spi_nor_prep_and_lock(nor);
	if (ret)
		return ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		goto out;

	nor->sst_write_second = false;

	/* Start write from odd address. */
	if (to % 2) {
		/* write one byte. */
		ret = sst_nor_write_data(nor, to, 1, buf);
		if (ret < 0)
			goto out;

		to++;
		actual++;
	}

	/* Write out most of the data here. */
	for (; actual < len - 1; actual += 2) {
		/* write two bytes. */
		ret = sst_nor_write_data(nor, to, 2, buf + actual);
		if (ret < 0)
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

		ret = sst_nor_write_data(nor, to, 1, buf + actual);
		if (ret < 0)
			goto out;

		actual += 1;

		ret = spi_nor_write_disable(nor);
	}
out:
	*retlen += actual;
	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int sst_nor_late_init(struct spi_nor *nor)
{
	if (nor->info->mfr_flags & SST_WRITE)
		nor->mtd._write = sst_nor_write;

	return 0;
}

static const struct spi_nor_fixups sst_nor_fixups = {
	.late_init = sst_nor_late_init,
};

const struct spi_nor_manufacturer spi_nor_sst = {
	.name = "sst",
	.parts = sst_nor_parts,
	.nparts = ARRAY_SIZE(sst_nor_parts),
	.fixups = &sst_nor_fixups,
};
