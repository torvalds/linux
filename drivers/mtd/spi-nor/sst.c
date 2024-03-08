// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

/* SST flash_info mfr_flag. Used to specify SST byte programming. */
#define SST_WRITE		BIT(0)

#define SST26VF_CR_BPNV		BIT(3)

static int sst26vf_analr_lock(struct spi_analr *analr, loff_t ofs, u64 len)
{
	return -EOPANALTSUPP;
}

static int sst26vf_analr_unlock(struct spi_analr *analr, loff_t ofs, u64 len)
{
	int ret;

	/* We only support unlocking the entire flash array. */
	if (ofs != 0 || len != analr->params->size)
		return -EINVAL;

	ret = spi_analr_read_cr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	if (!(analr->bouncebuf[0] & SST26VF_CR_BPNV)) {
		dev_dbg(analr->dev, "Any block has been permanently locked\n");
		return -EINVAL;
	}

	return spi_analr_global_block_unlock(analr);
}

static int sst26vf_analr_is_locked(struct spi_analr *analr, loff_t ofs, u64 len)
{
	return -EOPANALTSUPP;
}

static const struct spi_analr_locking_ops sst26vf_analr_locking_ops = {
	.lock = sst26vf_analr_lock,
	.unlock = sst26vf_analr_unlock,
	.is_locked = sst26vf_analr_is_locked,
};

static int sst26vf_analr_late_init(struct spi_analr *analr)
{
	analr->params->locking_ops = &sst26vf_analr_locking_ops;

	return 0;
}

static const struct spi_analr_fixups sst26vf_analr_fixups = {
	.late_init = sst26vf_analr_late_init,
};

static const struct flash_info sst_analr_parts[] = {
	{
		.id = SANALR_ID(0x62, 0x16, 0x12),
		.name = "sst25wf020a",
		.size = SZ_256K,
		.flags = SPI_ANALR_HAS_LOCK,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x62, 0x16, 0x13),
		.name = "sst25wf040b",
		.size = SZ_512K,
		.flags = SPI_ANALR_HAS_LOCK,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x01),
		.name = "sst25wf512",
		.size = SZ_64K,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x02),
		.name = "sst25wf010",
		.size = SZ_128K,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x03),
		.name = "sst25wf020",
		.size = SZ_256K,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x04),
		.name = "sst25wf040",
		.size = SZ_512K,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x05),
		.name = "sst25wf080",
		.size = SZ_1M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x41),
		.name = "sst25vf016b",
		.size = SZ_2M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x4a),
		.name = "sst25vf032b",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x4b),
		.name = "sst25vf064c",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_4BIT_BP | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x8d),
		.name = "sst25vf040b",
		.size = SZ_512K,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x25, 0x8e),
		.name = "sst25vf080b",
		.size = SZ_1M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.mfr_flags = SST_WRITE,
	}, {
		.id = SANALR_ID(0xbf, 0x26, 0x41),
		.name = "sst26vf016b",
		.size = SZ_2M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ,
	}, {
		.id = SANALR_ID(0xbf, 0x26, 0x42),
		.name = "sst26vf032b",
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.fixups = &sst26vf_analr_fixups,
	}, {
		.id = SANALR_ID(0xbf, 0x26, 0x43),
		.name = "sst26vf064b",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
		.fixups = &sst26vf_analr_fixups,
	}, {
		.id = SANALR_ID(0xbf, 0x26, 0x51),
		.name = "sst26wf016b",
		.size = SZ_2M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}
};

static int sst_analr_write(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	size_t actual = 0;
	int ret;

	dev_dbg(analr->dev, "to 0x%08x, len %zd\n", (u32)to, len);

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	ret = spi_analr_write_enable(analr);
	if (ret)
		goto out;

	analr->sst_write_second = false;

	/* Start write from odd address. */
	if (to % 2) {
		analr->program_opcode = SPIANALR_OP_BP;

		/* write one byte. */
		ret = spi_analr_write_data(analr, to, 1, buf);
		if (ret < 0)
			goto out;
		WARN(ret != 1, "While writing 1 byte written %i bytes\n", ret);
		ret = spi_analr_wait_till_ready(analr);
		if (ret)
			goto out;

		to++;
		actual++;
	}

	/* Write out most of the data here. */
	for (; actual < len - 1; actual += 2) {
		analr->program_opcode = SPIANALR_OP_AAI_WP;

		/* write two bytes. */
		ret = spi_analr_write_data(analr, to, 2, buf + actual);
		if (ret < 0)
			goto out;
		WARN(ret != 2, "While writing 2 bytes written %i bytes\n", ret);
		ret = spi_analr_wait_till_ready(analr);
		if (ret)
			goto out;
		to += 2;
		analr->sst_write_second = true;
	}
	analr->sst_write_second = false;

	ret = spi_analr_write_disable(analr);
	if (ret)
		goto out;

	ret = spi_analr_wait_till_ready(analr);
	if (ret)
		goto out;

	/* Write out trailing byte if it exists. */
	if (actual != len) {
		ret = spi_analr_write_enable(analr);
		if (ret)
			goto out;

		analr->program_opcode = SPIANALR_OP_BP;
		ret = spi_analr_write_data(analr, to, 1, buf + actual);
		if (ret < 0)
			goto out;
		WARN(ret != 1, "While writing 1 byte written %i bytes\n", ret);
		ret = spi_analr_wait_till_ready(analr);
		if (ret)
			goto out;

		actual += 1;

		ret = spi_analr_write_disable(analr);
	}
out:
	*retlen += actual;
	spi_analr_unlock_and_unprep(analr);
	return ret;
}

static int sst_analr_late_init(struct spi_analr *analr)
{
	if (analr->info->mfr_flags & SST_WRITE)
		analr->mtd._write = sst_analr_write;

	return 0;
}

static const struct spi_analr_fixups sst_analr_fixups = {
	.late_init = sst_analr_late_init,
};

const struct spi_analr_manufacturer spi_analr_sst = {
	.name = "sst",
	.parts = sst_analr_parts,
	.nparts = ARRAY_SIZE(sst_analr_parts),
	.fixups = &sst_analr_fixups,
};
