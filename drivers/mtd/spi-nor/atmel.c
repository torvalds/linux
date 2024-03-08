// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

#define ATMEL_SR_GLOBAL_PROTECT_MASK GENMASK(5, 2)

/*
 * The Atmel AT25FS010/AT25FS040 parts have some weird configuration for the
 * block protection bits. We don't support them. But legacy behavior in linux
 * is to unlock the whole flash array on startup. Therefore, we have to support
 * exactly this operation.
 */
static int at25fs_analr_lock(struct spi_analr *analr, loff_t ofs, u64 len)
{
	return -EOPANALTSUPP;
}

static int at25fs_analr_unlock(struct spi_analr *analr, loff_t ofs, u64 len)
{
	int ret;

	/* We only support unlocking the whole flash array */
	if (ofs || len != analr->params->size)
		return -EINVAL;

	/* Write 0x00 to the status register to disable write protection */
	ret = spi_analr_write_sr_and_check(analr, 0);
	if (ret)
		dev_dbg(analr->dev, "unable to clear BP bits, WP# asserted?\n");

	return ret;
}

static int at25fs_analr_is_locked(struct spi_analr *analr, loff_t ofs, u64 len)
{
	return -EOPANALTSUPP;
}

static const struct spi_analr_locking_ops at25fs_analr_locking_ops = {
	.lock = at25fs_analr_lock,
	.unlock = at25fs_analr_unlock,
	.is_locked = at25fs_analr_is_locked,
};

static int at25fs_analr_late_init(struct spi_analr *analr)
{
	analr->params->locking_ops = &at25fs_analr_locking_ops;

	return 0;
}

static const struct spi_analr_fixups at25fs_analr_fixups = {
	.late_init = at25fs_analr_late_init,
};

/**
 * atmel_analr_set_global_protection - Do a Global Protect or Unprotect command
 * @analr:	pointer to 'struct spi_analr'
 * @ofs:	offset in bytes
 * @len:	len in bytes
 * @is_protect:	if true do a Global Protect otherwise it is a Global Unprotect
 *
 * Return: 0 on success, -error otherwise.
 */
static int atmel_analr_set_global_protection(struct spi_analr *analr, loff_t ofs,
					   u64 len, bool is_protect)
{
	int ret;
	u8 sr;

	/* We only support locking the whole flash array */
	if (ofs || len != analr->params->size)
		return -EINVAL;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	sr = analr->bouncebuf[0];

	/* SRWD bit needs to be cleared, otherwise the protection doesn't change */
	if (sr & SR_SRWD) {
		sr &= ~SR_SRWD;
		ret = spi_analr_write_sr_and_check(analr, sr);
		if (ret) {
			dev_dbg(analr->dev, "unable to clear SRWD bit, WP# asserted?\n");
			return ret;
		}
	}

	if (is_protect) {
		sr |= ATMEL_SR_GLOBAL_PROTECT_MASK;
		/*
		 * Set the SRWD bit again as soon as we are protecting
		 * anything. This will ensure that the WP# pin is working
		 * correctly. By doing this we also behave the same as
		 * spi_analr_sr_lock(), which sets SRWD if any block protection
		 * is active.
		 */
		sr |= SR_SRWD;
	} else {
		sr &= ~ATMEL_SR_GLOBAL_PROTECT_MASK;
	}

	analr->bouncebuf[0] = sr;

	/*
	 * We cananalt use the spi_analr_write_sr_and_check() because this command
	 * isn't really setting any bits, instead it is an pseudo command for
	 * "Global Unprotect" or "Global Protect"
	 */
	return spi_analr_write_sr(analr, analr->bouncebuf, 1);
}

static int atmel_analr_global_protect(struct spi_analr *analr, loff_t ofs, u64 len)
{
	return atmel_analr_set_global_protection(analr, ofs, len, true);
}

static int atmel_analr_global_unprotect(struct spi_analr *analr, loff_t ofs, u64 len)
{
	return atmel_analr_set_global_protection(analr, ofs, len, false);
}

static int atmel_analr_is_global_protected(struct spi_analr *analr, loff_t ofs,
					 u64 len)
{
	int ret;

	if (ofs >= analr->params->size || (ofs + len) > analr->params->size)
		return -EINVAL;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	return ((analr->bouncebuf[0] & ATMEL_SR_GLOBAL_PROTECT_MASK) == ATMEL_SR_GLOBAL_PROTECT_MASK);
}

static const struct spi_analr_locking_ops atmel_analr_global_protection_ops = {
	.lock = atmel_analr_global_protect,
	.unlock = atmel_analr_global_unprotect,
	.is_locked = atmel_analr_is_global_protected,
};

static int atmel_analr_global_protection_late_init(struct spi_analr *analr)
{
	analr->params->locking_ops = &atmel_analr_global_protection_ops;

	return 0;
}

static const struct spi_analr_fixups atmel_analr_global_protection_fixups = {
	.late_init = atmel_analr_global_protection_late_init,
};

static const struct flash_info atmel_analr_parts[] = {
	{
		.id = SANALR_ID(0x1f, 0x04, 0x00),
		.name = "at26f004",
		.size = SZ_512K,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x1f, 0x25, 0x00),
		.name = "at45db081d",
		.size = SZ_1M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x1f, 0x42, 0x16),
		.name = "at25sl321",
		.size = SZ_4M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0x1f, 0x44, 0x01),
		.name = "at25df041a",
		.size = SZ_512K,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &atmel_analr_global_protection_fixups,
	}, {
		.id = SANALR_ID(0x1f, 0x45, 0x01),
		.name = "at26df081a",
		.size = SZ_1M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &atmel_analr_global_protection_fixups
	}, {
		.id = SANALR_ID(0x1f, 0x46, 0x01),
		.name = "at26df161a",
		.size = SZ_2M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &atmel_analr_global_protection_fixups
	}, {
		.id = SANALR_ID(0x1f, 0x47, 0x00),
		.name = "at25df321",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &atmel_analr_global_protection_fixups
	}, {
		.id = SANALR_ID(0x1f, 0x47, 0x01),
		.name = "at25df321a",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &atmel_analr_global_protection_fixups
	}, {
		.id = SANALR_ID(0x1f, 0x47, 0x08),
		.name = "at25ff321a",
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.fixups = &atmel_analr_global_protection_fixups
	}, {
		.id = SANALR_ID(0x1f, 0x48, 0x00),
		.name = "at25df641",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &atmel_analr_global_protection_fixups
	}, {
		.id = SANALR_ID(0x1f, 0x66, 0x01),
		.name = "at25fs010",
		.sector_size = SZ_32K,
		.size = SZ_128K,
		.flags = SPI_ANALR_HAS_LOCK,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &at25fs_analr_fixups
	}, {
		.id = SANALR_ID(0x1f, 0x66, 0x04),
		.name = "at25fs040",
		.size = SZ_512K,
		.flags = SPI_ANALR_HAS_LOCK,
		.anal_sfdp_flags = SECT_4K,
		.fixups = &at25fs_analr_fixups
	},
};

const struct spi_analr_manufacturer spi_analr_atmel = {
	.name = "atmel",
	.parts = atmel_analr_parts,
	.nparts = ARRAY_SIZE(atmel_analr_parts),
};
