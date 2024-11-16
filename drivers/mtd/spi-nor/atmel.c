// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define ATMEL_SR_GLOBAL_PROTECT_MASK GENMASK(5, 2)

/*
 * The Atmel AT25FS010/AT25FS040 parts have some weird configuration for the
 * block protection bits. We don't support them. But legacy behavior in linux
 * is to unlock the whole flash array on startup. Therefore, we have to support
 * exactly this operation.
 */
static int at25fs_nor_lock(struct spi_nor *nor, loff_t ofs, u64 len)
{
	return -EOPNOTSUPP;
}

static int at25fs_nor_unlock(struct spi_nor *nor, loff_t ofs, u64 len)
{
	int ret;

	/* We only support unlocking the whole flash array */
	if (ofs || len != nor->params->size)
		return -EINVAL;

	/* Write 0x00 to the status register to disable write protection */
	ret = spi_nor_write_sr_and_check(nor, 0);
	if (ret)
		dev_dbg(nor->dev, "unable to clear BP bits, WP# asserted?\n");

	return ret;
}

static int at25fs_nor_is_locked(struct spi_nor *nor, loff_t ofs, u64 len)
{
	return -EOPNOTSUPP;
}

static const struct spi_nor_locking_ops at25fs_nor_locking_ops = {
	.lock = at25fs_nor_lock,
	.unlock = at25fs_nor_unlock,
	.is_locked = at25fs_nor_is_locked,
};

static int at25fs_nor_late_init(struct spi_nor *nor)
{
	nor->params->locking_ops = &at25fs_nor_locking_ops;

	return 0;
}

static const struct spi_nor_fixups at25fs_nor_fixups = {
	.late_init = at25fs_nor_late_init,
};

/**
 * atmel_nor_set_global_protection - Do a Global Protect or Unprotect command
 * @nor:	pointer to 'struct spi_nor'
 * @ofs:	offset in bytes
 * @len:	len in bytes
 * @is_protect:	if true do a Global Protect otherwise it is a Global Unprotect
 *
 * Return: 0 on success, -error otherwise.
 */
static int atmel_nor_set_global_protection(struct spi_nor *nor, loff_t ofs,
					   u64 len, bool is_protect)
{
	int ret;
	u8 sr;

	/* We only support locking the whole flash array */
	if (ofs || len != nor->params->size)
		return -EINVAL;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	sr = nor->bouncebuf[0];

	/* SRWD bit needs to be cleared, otherwise the protection doesn't change */
	if (sr & SR_SRWD) {
		sr &= ~SR_SRWD;
		ret = spi_nor_write_sr_and_check(nor, sr);
		if (ret) {
			dev_dbg(nor->dev, "unable to clear SRWD bit, WP# asserted?\n");
			return ret;
		}
	}

	if (is_protect) {
		sr |= ATMEL_SR_GLOBAL_PROTECT_MASK;
		/*
		 * Set the SRWD bit again as soon as we are protecting
		 * anything. This will ensure that the WP# pin is working
		 * correctly. By doing this we also behave the same as
		 * spi_nor_sr_lock(), which sets SRWD if any block protection
		 * is active.
		 */
		sr |= SR_SRWD;
	} else {
		sr &= ~ATMEL_SR_GLOBAL_PROTECT_MASK;
	}

	nor->bouncebuf[0] = sr;

	/*
	 * We cannot use the spi_nor_write_sr_and_check() because this command
	 * isn't really setting any bits, instead it is an pseudo command for
	 * "Global Unprotect" or "Global Protect"
	 */
	return spi_nor_write_sr(nor, nor->bouncebuf, 1);
}

static int atmel_nor_global_protect(struct spi_nor *nor, loff_t ofs, u64 len)
{
	return atmel_nor_set_global_protection(nor, ofs, len, true);
}

static int atmel_nor_global_unprotect(struct spi_nor *nor, loff_t ofs, u64 len)
{
	return atmel_nor_set_global_protection(nor, ofs, len, false);
}

static int atmel_nor_is_global_protected(struct spi_nor *nor, loff_t ofs,
					 u64 len)
{
	int ret;

	if (ofs >= nor->params->size || (ofs + len) > nor->params->size)
		return -EINVAL;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	return ((nor->bouncebuf[0] & ATMEL_SR_GLOBAL_PROTECT_MASK) == ATMEL_SR_GLOBAL_PROTECT_MASK);
}

static const struct spi_nor_locking_ops atmel_nor_global_protection_ops = {
	.lock = atmel_nor_global_protect,
	.unlock = atmel_nor_global_unprotect,
	.is_locked = atmel_nor_is_global_protected,
};

static int atmel_nor_global_protection_late_init(struct spi_nor *nor)
{
	nor->params->locking_ops = &atmel_nor_global_protection_ops;

	return 0;
}

static const struct spi_nor_fixups atmel_nor_global_protection_fixups = {
	.late_init = atmel_nor_global_protection_late_init,
};

static const struct flash_info atmel_nor_parts[] = {
	{
		.id = SNOR_ID(0x1f, 0x04, 0x00),
		.name = "at26f004",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x1f, 0x25, 0x00),
		.name = "at45db081d",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x1f, 0x42, 0x16),
		.name = "at25sl321",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x1f, 0x44, 0x01),
		.name = "at25df041a",
		.size = SZ_512K,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.fixups = &atmel_nor_global_protection_fixups,
	}, {
		.id = SNOR_ID(0x1f, 0x45, 0x01),
		.name = "at26df081a",
		.size = SZ_1M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.fixups = &atmel_nor_global_protection_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x46, 0x01),
		.name = "at26df161a",
		.size = SZ_2M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.fixups = &atmel_nor_global_protection_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x47, 0x00),
		.name = "at25df321",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.fixups = &atmel_nor_global_protection_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x47, 0x01),
		.name = "at25df321a",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.fixups = &atmel_nor_global_protection_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x47, 0x08),
		.name = "at25ff321a",
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.fixups = &atmel_nor_global_protection_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x48, 0x00),
		.name = "at25df641",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
		.fixups = &atmel_nor_global_protection_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x66, 0x01),
		.name = "at25fs010",
		.sector_size = SZ_32K,
		.size = SZ_128K,
		.flags = SPI_NOR_HAS_LOCK,
		.no_sfdp_flags = SECT_4K,
		.fixups = &at25fs_nor_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x66, 0x04),
		.name = "at25fs040",
		.size = SZ_512K,
		.flags = SPI_NOR_HAS_LOCK,
		.no_sfdp_flags = SECT_4K,
		.fixups = &at25fs_nor_fixups
	}, {
		.id = SNOR_ID(0x1f, 0x87, 0x01),
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	},
};

const struct spi_nor_manufacturer spi_nor_atmel = {
	.name = "atmel",
	.parts = atmel_nor_parts,
	.nparts = ARRAY_SIZE(atmel_nor_parts),
};
