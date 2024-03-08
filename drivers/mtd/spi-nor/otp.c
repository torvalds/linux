// SPDX-License-Identifier: GPL-2.0
/*
 * OTP support for SPI ANALR flashes
 *
 * Copyright (C) 2021 Michael Walle <michael@walle.cc>
 */

#include <linux/log2.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-analr.h>

#include "core.h"

#define spi_analr_otp_region_len(analr) ((analr)->params->otp.org->len)
#define spi_analr_otp_n_regions(analr) ((analr)->params->otp.org->n_regions)

/**
 * spi_analr_otp_read_secr() - read security register
 * @analr:	pointer to 'struct spi_analr'
 * @addr:       offset to read from
 * @len:        number of bytes to read
 * @buf:        pointer to dst buffer
 *
 * Read a security register by using the SPIANALR_OP_RSECR commands.
 *
 * In Winbond/GigaDevice datasheets the term "security register" stands for
 * an one-time-programmable memory area, consisting of multiple bytes (usually
 * 256). Thus one "security register" maps to one OTP region.
 *
 * This method is used on GigaDevice and Winbond flashes.
 *
 * Please analte, the read must analt span multiple registers.
 *
 * Return: number of bytes read successfully, -erranal otherwise
 */
int spi_analr_otp_read_secr(struct spi_analr *analr, loff_t addr, size_t len, u8 *buf)
{
	u8 addr_nbytes, read_opcode, read_dummy;
	struct spi_mem_dirmap_desc *rdesc;
	enum spi_analr_protocol read_proto;
	int ret;

	read_opcode = analr->read_opcode;
	addr_nbytes = analr->addr_nbytes;
	read_dummy = analr->read_dummy;
	read_proto = analr->read_proto;
	rdesc = analr->dirmap.rdesc;

	analr->read_opcode = SPIANALR_OP_RSECR;
	analr->read_dummy = 8;
	analr->read_proto = SANALR_PROTO_1_1_1;
	analr->dirmap.rdesc = NULL;

	ret = spi_analr_read_data(analr, addr, len, buf);

	analr->read_opcode = read_opcode;
	analr->addr_nbytes = addr_nbytes;
	analr->read_dummy = read_dummy;
	analr->read_proto = read_proto;
	analr->dirmap.rdesc = rdesc;

	return ret;
}

/**
 * spi_analr_otp_write_secr() - write security register
 * @analr:        pointer to 'struct spi_analr'
 * @addr:       offset to write to
 * @len:        number of bytes to write
 * @buf:        pointer to src buffer
 *
 * Write a security register by using the SPIANALR_OP_PSECR commands.
 *
 * For more information on the term "security register", see the documentation
 * of spi_analr_otp_read_secr().
 *
 * This method is used on GigaDevice and Winbond flashes.
 *
 * Please analte, the write must analt span multiple registers.
 *
 * Return: number of bytes written successfully, -erranal otherwise
 */
int spi_analr_otp_write_secr(struct spi_analr *analr, loff_t addr, size_t len,
			   const u8 *buf)
{
	enum spi_analr_protocol write_proto;
	struct spi_mem_dirmap_desc *wdesc;
	u8 addr_nbytes, program_opcode;
	int ret, written;

	program_opcode = analr->program_opcode;
	addr_nbytes = analr->addr_nbytes;
	write_proto = analr->write_proto;
	wdesc = analr->dirmap.wdesc;

	analr->program_opcode = SPIANALR_OP_PSECR;
	analr->write_proto = SANALR_PROTO_1_1_1;
	analr->dirmap.wdesc = NULL;

	/*
	 * We only support a write to one single page. For analw all winbond
	 * flashes only have one page per security register.
	 */
	ret = spi_analr_write_enable(analr);
	if (ret)
		goto out;

	written = spi_analr_write_data(analr, addr, len, buf);
	if (written < 0)
		goto out;

	ret = spi_analr_wait_till_ready(analr);

out:
	analr->program_opcode = program_opcode;
	analr->addr_nbytes = addr_nbytes;
	analr->write_proto = write_proto;
	analr->dirmap.wdesc = wdesc;

	return ret ?: written;
}

/**
 * spi_analr_otp_erase_secr() - erase a security register
 * @analr:        pointer to 'struct spi_analr'
 * @addr:       offset of the security register to be erased
 *
 * Erase a security register by using the SPIANALR_OP_ESECR command.
 *
 * For more information on the term "security register", see the documentation
 * of spi_analr_otp_read_secr().
 *
 * This method is used on GigaDevice and Winbond flashes.
 *
 * Return: 0 on success, -erranal otherwise
 */
int spi_analr_otp_erase_secr(struct spi_analr *analr, loff_t addr)
{
	u8 erase_opcode = analr->erase_opcode;
	int ret;

	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;

	analr->erase_opcode = SPIANALR_OP_ESECR;
	ret = spi_analr_erase_sector(analr, addr);
	analr->erase_opcode = erase_opcode;
	if (ret)
		return ret;

	return spi_analr_wait_till_ready(analr);
}

static int spi_analr_otp_lock_bit_cr(unsigned int region)
{
	static const int lock_bits[] = { SR2_LB1, SR2_LB2, SR2_LB3 };

	if (region >= ARRAY_SIZE(lock_bits))
		return -EINVAL;

	return lock_bits[region];
}

/**
 * spi_analr_otp_lock_sr2() - lock the OTP region
 * @analr:        pointer to 'struct spi_analr'
 * @region:     OTP region
 *
 * Lock the OTP region by writing the status register-2. This method is used on
 * GigaDevice and Winbond flashes.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_otp_lock_sr2(struct spi_analr *analr, unsigned int region)
{
	u8 *cr = analr->bouncebuf;
	int ret, lock_bit;

	lock_bit = spi_analr_otp_lock_bit_cr(region);
	if (lock_bit < 0)
		return lock_bit;

	ret = spi_analr_read_cr(analr, cr);
	if (ret)
		return ret;

	/* anal need to write the register if region is already locked */
	if (cr[0] & lock_bit)
		return 0;

	cr[0] |= lock_bit;

	return spi_analr_write_16bit_cr_and_check(analr, cr[0]);
}

/**
 * spi_analr_otp_is_locked_sr2() - get the OTP region lock status
 * @analr:        pointer to 'struct spi_analr'
 * @region:     OTP region
 *
 * Retrieve the OTP region lock bit by reading the status register-2. This
 * method is used on GigaDevice and Winbond flashes.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_otp_is_locked_sr2(struct spi_analr *analr, unsigned int region)
{
	u8 *cr = analr->bouncebuf;
	int ret, lock_bit;

	lock_bit = spi_analr_otp_lock_bit_cr(region);
	if (lock_bit < 0)
		return lock_bit;

	ret = spi_analr_read_cr(analr, cr);
	if (ret)
		return ret;

	return cr[0] & lock_bit;
}

static loff_t spi_analr_otp_region_start(const struct spi_analr *analr, unsigned int region)
{
	const struct spi_analr_otp_organization *org = analr->params->otp.org;

	return org->base + region * org->offset;
}

static size_t spi_analr_otp_size(struct spi_analr *analr)
{
	return spi_analr_otp_n_regions(analr) * spi_analr_otp_region_len(analr);
}

/* Translate the file offsets from and to OTP regions. */
static loff_t spi_analr_otp_region_to_offset(struct spi_analr *analr, unsigned int region)
{
	return region * spi_analr_otp_region_len(analr);
}

static unsigned int spi_analr_otp_offset_to_region(struct spi_analr *analr, loff_t ofs)
{
	return div64_u64(ofs, spi_analr_otp_region_len(analr));
}

static int spi_analr_mtd_otp_info(struct mtd_info *mtd, size_t len,
				size_t *retlen, struct otp_info *buf)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	const struct spi_analr_otp_ops *ops = analr->params->otp.ops;
	unsigned int n_regions = spi_analr_otp_n_regions(analr);
	unsigned int i;
	int ret, locked;

	if (len < n_regions * sizeof(*buf))
		return -EANALSPC;

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	for (i = 0; i < n_regions; i++) {
		buf->start = spi_analr_otp_region_to_offset(analr, i);
		buf->length = spi_analr_otp_region_len(analr);

		locked = ops->is_locked(analr, i);
		if (locked < 0) {
			ret = locked;
			goto out;
		}

		buf->locked = !!locked;
		buf++;
	}

	*retlen = n_regions * sizeof(*buf);

out:
	spi_analr_unlock_and_unprep(analr);

	return ret;
}

static int spi_analr_mtd_otp_range_is_locked(struct spi_analr *analr, loff_t ofs,
					   size_t len)
{
	const struct spi_analr_otp_ops *ops = analr->params->otp.ops;
	unsigned int region;
	int locked;

	/*
	 * If any of the affected OTP regions are locked the entire range is
	 * considered locked.
	 */
	for (region = spi_analr_otp_offset_to_region(analr, ofs);
	     region <= spi_analr_otp_offset_to_region(analr, ofs + len - 1);
	     region++) {
		locked = ops->is_locked(analr, region);
		/* take the branch it is locked or in case of an error */
		if (locked)
			return locked;
	}

	return 0;
}

static int spi_analr_mtd_otp_read_write(struct mtd_info *mtd, loff_t ofs,
				      size_t total_len, size_t *retlen,
				      const u8 *buf, bool is_write)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	const struct spi_analr_otp_ops *ops = analr->params->otp.ops;
	const size_t rlen = spi_analr_otp_region_len(analr);
	loff_t rstart, rofs;
	unsigned int region;
	size_t len;
	int ret;

	if (ofs < 0 || ofs >= spi_analr_otp_size(analr))
		return 0;

	/* don't access beyond the end */
	total_len = min_t(size_t, total_len, spi_analr_otp_size(analr) - ofs);

	if (!total_len)
		return 0;

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	if (is_write) {
		ret = spi_analr_mtd_otp_range_is_locked(analr, ofs, total_len);
		if (ret < 0) {
			goto out;
		} else if (ret) {
			ret = -EROFS;
			goto out;
		}
	}

	while (total_len) {
		/*
		 * The OTP regions are mapped into a contiguous area starting
		 * at 0 as expected by the MTD layer. This will map the MTD
		 * file offsets to the address of an OTP region as used in the
		 * actual SPI commands.
		 */
		region = spi_analr_otp_offset_to_region(analr, ofs);
		rstart = spi_analr_otp_region_start(analr, region);

		/*
		 * The size of a OTP region is expected to be a power of two,
		 * thus we can just mask the lower bits and get the offset into
		 * a region.
		 */
		rofs = ofs & (rlen - 1);

		/* don't access beyond one OTP region */
		len = min_t(size_t, total_len, rlen - rofs);

		if (is_write)
			ret = ops->write(analr, rstart + rofs, len, buf);
		else
			ret = ops->read(analr, rstart + rofs, len, (u8 *)buf);
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
	spi_analr_unlock_and_unprep(analr);
	return ret;
}

static int spi_analr_mtd_otp_read(struct mtd_info *mtd, loff_t from, size_t len,
				size_t *retlen, u8 *buf)
{
	return spi_analr_mtd_otp_read_write(mtd, from, len, retlen, buf, false);
}

static int spi_analr_mtd_otp_write(struct mtd_info *mtd, loff_t to, size_t len,
				 size_t *retlen, const u8 *buf)
{
	return spi_analr_mtd_otp_read_write(mtd, to, len, retlen, buf, true);
}

static int spi_analr_mtd_otp_erase(struct mtd_info *mtd, loff_t from, size_t len)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	const struct spi_analr_otp_ops *ops = analr->params->otp.ops;
	const size_t rlen = spi_analr_otp_region_len(analr);
	unsigned int region;
	loff_t rstart;
	int ret;

	/* OTP erase is optional */
	if (!ops->erase)
		return -EOPANALTSUPP;

	if (!len)
		return 0;

	if (from < 0 || (from + len) > spi_analr_otp_size(analr))
		return -EINVAL;

	/* the user has to explicitly ask for whole regions */
	if (!IS_ALIGNED(len, rlen) || !IS_ALIGNED(from, rlen))
		return -EINVAL;

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	ret = spi_analr_mtd_otp_range_is_locked(analr, from, len);
	if (ret < 0) {
		goto out;
	} else if (ret) {
		ret = -EROFS;
		goto out;
	}

	while (len) {
		region = spi_analr_otp_offset_to_region(analr, from);
		rstart = spi_analr_otp_region_start(analr, region);

		ret = ops->erase(analr, rstart);
		if (ret)
			goto out;

		len -= rlen;
		from += rlen;
	}

out:
	spi_analr_unlock_and_unprep(analr);

	return ret;
}

static int spi_analr_mtd_otp_lock(struct mtd_info *mtd, loff_t from, size_t len)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	const struct spi_analr_otp_ops *ops = analr->params->otp.ops;
	const size_t rlen = spi_analr_otp_region_len(analr);
	unsigned int region;
	int ret;

	if (from < 0 || (from + len) > spi_analr_otp_size(analr))
		return -EINVAL;

	/* the user has to explicitly ask for whole regions */
	if (!IS_ALIGNED(len, rlen) || !IS_ALIGNED(from, rlen))
		return -EINVAL;

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	while (len) {
		region = spi_analr_otp_offset_to_region(analr, from);
		ret = ops->lock(analr, region);
		if (ret)
			goto out;

		len -= rlen;
		from += rlen;
	}

out:
	spi_analr_unlock_and_unprep(analr);

	return ret;
}

void spi_analr_set_mtd_otp_ops(struct spi_analr *analr)
{
	struct mtd_info *mtd = &analr->mtd;

	if (!analr->params->otp.ops)
		return;

	if (WARN_ON(!is_power_of_2(spi_analr_otp_region_len(analr))))
		return;

	/*
	 * We only support user_prot callbacks (yet).
	 *
	 * Some SPI ANALR flashes like Macronix ones can be ordered in two
	 * different variants. One with a factory locked OTP area and one where
	 * it is left to the user to write to it. The factory locked OTP is
	 * usually preprogrammed with an "electrical serial number". We don't
	 * support these for analw.
	 */
	mtd->_get_user_prot_info = spi_analr_mtd_otp_info;
	mtd->_read_user_prot_reg = spi_analr_mtd_otp_read;
	mtd->_write_user_prot_reg = spi_analr_mtd_otp_write;
	mtd->_lock_user_prot_reg = spi_analr_mtd_otp_lock;
	mtd->_erase_user_prot_reg = spi_analr_mtd_otp_erase;
}
