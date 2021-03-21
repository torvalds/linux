// SPDX-License-Identifier: GPL-2.0
/*
 * OTP support for SPI NOR flashes
 *
 * Copyright (C) 2021 Michael Walle <michael@walle.cc>
 */

#include <linux/log2.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>

#include "core.h"

#define spi_nor_otp_region_len(nor) ((nor)->params->otp.org->len)
#define spi_nor_otp_n_regions(nor) ((nor)->params->otp.org->n_regions)

static loff_t spi_nor_otp_region_start(const struct spi_nor *nor, unsigned int region)
{
	const struct spi_nor_otp_organization *org = nor->params->otp.org;

	return org->base + region * org->offset;
}

static size_t spi_nor_otp_size(struct spi_nor *nor)
{
	return spi_nor_otp_n_regions(nor) * spi_nor_otp_region_len(nor);
}

/* Translate the file offsets from and to OTP regions. */
static loff_t spi_nor_otp_region_to_offset(struct spi_nor *nor, unsigned int region)
{
	return region * spi_nor_otp_region_len(nor);
}

static unsigned int spi_nor_otp_offset_to_region(struct spi_nor *nor, loff_t ofs)
{
	return div64_u64(ofs, spi_nor_otp_region_len(nor));
}

static int spi_nor_mtd_otp_info(struct mtd_info *mtd, size_t len,
				size_t *retlen, struct otp_info *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	const struct spi_nor_otp_ops *ops = nor->params->otp.ops;
	unsigned int n_regions = spi_nor_otp_n_regions(nor);
	unsigned int i;
	int ret, locked;

	if (len < n_regions * sizeof(*buf))
		return -ENOSPC;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	for (i = 0; i < n_regions; i++) {
		buf->start = spi_nor_otp_region_to_offset(nor, i);
		buf->length = spi_nor_otp_region_len(nor);

		locked = ops->is_locked(nor, i);
		if (locked < 0) {
			ret = locked;
			goto out;
		}

		buf->locked = !!locked;
		buf++;
	}

	*retlen = n_regions * sizeof(*buf);

out:
	spi_nor_unlock_and_unprep(nor);

	return ret;
}

static int spi_nor_mtd_otp_read_write(struct mtd_info *mtd, loff_t ofs,
				      size_t total_len, size_t *retlen,
				      u8 *buf, bool is_write)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	const struct spi_nor_otp_ops *ops = nor->params->otp.ops;
	const size_t rlen = spi_nor_otp_region_len(nor);
	loff_t rstart, rofs;
	unsigned int region;
	size_t len;
	int ret;

	if (ofs < 0 || ofs >= spi_nor_otp_size(nor))
		return 0;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	/* don't access beyond the end */
	total_len = min_t(size_t, total_len, spi_nor_otp_size(nor) - ofs);

	*retlen = 0;
	while (total_len) {
		/*
		 * The OTP regions are mapped into a contiguous area starting
		 * at 0 as expected by the MTD layer. This will map the MTD
		 * file offsets to the address of an OTP region as used in the
		 * actual SPI commands.
		 */
		region = spi_nor_otp_offset_to_region(nor, ofs);
		rstart = spi_nor_otp_region_start(nor, region);

		/*
		 * The size of a OTP region is expected to be a power of two,
		 * thus we can just mask the lower bits and get the offset into
		 * a region.
		 */
		rofs = ofs & (rlen - 1);

		/* don't access beyond one OTP region */
		len = min_t(size_t, total_len, rlen - rofs);

		if (is_write)
			ret = ops->write(nor, rstart + rofs, len, buf);
		else
			ret = ops->read(nor, rstart + rofs, len, buf);
		if (ret == 0)
			ret = -EIO;
		if (ret < 0)
			goto out;

		*retlen += ret;
		ofs += ret;
		buf += ret;
		total_len -= ret;
	}
	ret = 0;

out:
	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_mtd_otp_read(struct mtd_info *mtd, loff_t from, size_t len,
				size_t *retlen, u8 *buf)
{
	return spi_nor_mtd_otp_read_write(mtd, from, len, retlen, buf, false);
}

static int spi_nor_mtd_otp_write(struct mtd_info *mtd, loff_t to, size_t len,
				 size_t *retlen, u8 *buf)
{
	return spi_nor_mtd_otp_read_write(mtd, to, len, retlen, buf, true);
}

static int spi_nor_mtd_otp_lock(struct mtd_info *mtd, loff_t from, size_t len)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	const struct spi_nor_otp_ops *ops = nor->params->otp.ops;
	const size_t rlen = spi_nor_otp_region_len(nor);
	unsigned int region;
	int ret;

	if (from < 0 || (from + len) > spi_nor_otp_size(nor))
		return -EINVAL;

	/* the user has to explicitly ask for whole regions */
	if (!IS_ALIGNED(len, rlen) || !IS_ALIGNED(from, rlen))
		return -EINVAL;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	while (len) {
		region = spi_nor_otp_offset_to_region(nor, from);
		ret = ops->lock(nor, region);
		if (ret)
			goto out;

		len -= rlen;
		from += rlen;
	}

out:
	spi_nor_unlock_and_unprep(nor);

	return ret;
}

void spi_nor_otp_init(struct spi_nor *nor)
{
	struct mtd_info *mtd = &nor->mtd;

	if (!nor->params->otp.ops)
		return;

	if (WARN_ON(!is_power_of_2(spi_nor_otp_region_len(nor))))
		return;

	/*
	 * We only support user_prot callbacks (yet).
	 *
	 * Some SPI NOR flashes like Macronix ones can be ordered in two
	 * different variants. One with a factory locked OTP area and one where
	 * it is left to the user to write to it. The factory locked OTP is
	 * usually preprogrammed with an "electrical serial number". We don't
	 * support these for now.
	 */
	mtd->_get_user_prot_info = spi_nor_mtd_otp_info;
	mtd->_read_user_prot_reg = spi_nor_mtd_otp_read;
	mtd->_write_user_prot_reg = spi_nor_mtd_otp_write;
	mtd->_lock_user_prot_reg = spi_nor_mtd_otp_lock;
}
