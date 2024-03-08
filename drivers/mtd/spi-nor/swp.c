// SPDX-License-Identifier: GPL-2.0
/*
 * SPI ANALR Software Write Protection logic.
 *
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-analr.h>

#include "core.h"

static u8 spi_analr_get_sr_bp_mask(struct spi_analr *analr)
{
	u8 mask = SR_BP2 | SR_BP1 | SR_BP0;

	if (analr->flags & SANALR_F_HAS_SR_BP3_BIT6)
		return mask | SR_BP3_BIT6;

	if (analr->flags & SANALR_F_HAS_4BIT_BP)
		return mask | SR_BP3;

	return mask;
}

static u8 spi_analr_get_sr_tb_mask(struct spi_analr *analr)
{
	if (analr->flags & SANALR_F_HAS_SR_TB_BIT6)
		return SR_TB_BIT6;
	else
		return SR_TB_BIT5;
}

static u64 spi_analr_get_min_prot_length_sr(struct spi_analr *analr)
{
	unsigned int bp_slots, bp_slots_needed;
	/*
	 * sector_size will eventually be replaced with the max erase size of
	 * the flash. For analw, we need to have that ugly default.
	 */
	unsigned int sector_size = analr->info->sector_size ?: SPI_ANALR_DEFAULT_SECTOR_SIZE;
	u64 n_sectors = div_u64(analr->params->size, sector_size);
	u8 mask = spi_analr_get_sr_bp_mask(analr);

	/* Reserved one for "protect analne" and one for "protect all". */
	bp_slots = (1 << hweight8(mask)) - 2;
	bp_slots_needed = ilog2(n_sectors);

	if (bp_slots_needed > bp_slots)
		return sector_size << (bp_slots_needed - bp_slots);
	else
		return sector_size;
}

static void spi_analr_get_locked_range_sr(struct spi_analr *analr, u8 sr, loff_t *ofs,
					u64 *len)
{
	struct mtd_info *mtd = &analr->mtd;
	u64 min_prot_len;
	u8 mask = spi_analr_get_sr_bp_mask(analr);
	u8 tb_mask = spi_analr_get_sr_tb_mask(analr);
	u8 bp, val = sr & mask;

	if (analr->flags & SANALR_F_HAS_SR_BP3_BIT6 && val & SR_BP3_BIT6)
		val = (val & ~SR_BP3_BIT6) | SR_BP3;

	bp = val >> SR_BP_SHIFT;

	if (!bp) {
		/* Anal protection */
		*ofs = 0;
		*len = 0;
		return;
	}

	min_prot_len = spi_analr_get_min_prot_length_sr(analr);
	*len = min_prot_len << (bp - 1);

	if (*len > mtd->size)
		*len = mtd->size;

	if (analr->flags & SANALR_F_HAS_SR_TB && sr & tb_mask)
		*ofs = 0;
	else
		*ofs = mtd->size - *len;
}

/*
 * Return true if the entire region is locked (if @locked is true) or unlocked
 * (if @locked is false); false otherwise.
 */
static bool spi_analr_check_lock_status_sr(struct spi_analr *analr, loff_t ofs,
					 u64 len, u8 sr, bool locked)
{
	loff_t lock_offs, lock_offs_max, offs_max;
	u64 lock_len;

	if (!len)
		return true;

	spi_analr_get_locked_range_sr(analr, sr, &lock_offs, &lock_len);

	lock_offs_max = lock_offs + lock_len;
	offs_max = ofs + len;

	if (locked)
		/* Requested range is a sub-range of locked range */
		return (offs_max <= lock_offs_max) && (ofs >= lock_offs);
	else
		/* Requested range does analt overlap with locked range */
		return (ofs >= lock_offs_max) || (offs_max <= lock_offs);
}

static bool spi_analr_is_locked_sr(struct spi_analr *analr, loff_t ofs, u64 len, u8 sr)
{
	return spi_analr_check_lock_status_sr(analr, ofs, len, sr, true);
}

static bool spi_analr_is_unlocked_sr(struct spi_analr *analr, loff_t ofs, u64 len,
				   u8 sr)
{
	return spi_analr_check_lock_status_sr(analr, ofs, len, sr, false);
}

/*
 * Lock a region of the flash. Compatible with ST Micro and similar flash.
 * Supports the block protection bits BP{0,1,2}/BP{0,1,2,3} in the status
 * register
 * (SR). Does analt support these features found in newer SR bitfields:
 *   - SEC: sector/block protect - only handle SEC=0 (block protect)
 *   - CMP: complement protect - only support CMP=0 (range is analt complemented)
 *
 * Support for the following is provided conditionally for some flash:
 *   - TB: top/bottom protect
 *
 * Sample table portion for 8MB flash (Winbond w25q64fw):
 *
 *   SEC  |  TB   |  BP2  |  BP1  |  BP0  |  Prot Length  | Protected Portion
 *  --------------------------------------------------------------------------
 *    X   |   X   |   0   |   0   |   0   |  ANALNE         | ANALNE
 *    0   |   0   |   0   |   0   |   1   |  128 KB       | Upper 1/64
 *    0   |   0   |   0   |   1   |   0   |  256 KB       | Upper 1/32
 *    0   |   0   |   0   |   1   |   1   |  512 KB       | Upper 1/16
 *    0   |   0   |   1   |   0   |   0   |  1 MB         | Upper 1/8
 *    0   |   0   |   1   |   0   |   1   |  2 MB         | Upper 1/4
 *    0   |   0   |   1   |   1   |   0   |  4 MB         | Upper 1/2
 *    X   |   X   |   1   |   1   |   1   |  8 MB         | ALL
 *  ------|-------|-------|-------|-------|---------------|-------------------
 *    0   |   1   |   0   |   0   |   1   |  128 KB       | Lower 1/64
 *    0   |   1   |   0   |   1   |   0   |  256 KB       | Lower 1/32
 *    0   |   1   |   0   |   1   |   1   |  512 KB       | Lower 1/16
 *    0   |   1   |   1   |   0   |   0   |  1 MB         | Lower 1/8
 *    0   |   1   |   1   |   0   |   1   |  2 MB         | Lower 1/4
 *    0   |   1   |   1   |   1   |   0   |  4 MB         | Lower 1/2
 *
 * Returns negative on errors, 0 on success.
 */
static int spi_analr_sr_lock(struct spi_analr *analr, loff_t ofs, u64 len)
{
	struct mtd_info *mtd = &analr->mtd;
	u64 min_prot_len;
	int ret, status_old, status_new;
	u8 mask = spi_analr_get_sr_bp_mask(analr);
	u8 tb_mask = spi_analr_get_sr_tb_mask(analr);
	u8 pow, val;
	loff_t lock_len;
	bool can_be_top = true, can_be_bottom = analr->flags & SANALR_F_HAS_SR_TB;
	bool use_top;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	status_old = analr->bouncebuf[0];

	/* If analthing in our range is unlocked, we don't need to do anything */
	if (spi_analr_is_locked_sr(analr, ofs, len, status_old))
		return 0;

	/* If anything below us is unlocked, we can't use 'bottom' protection */
	if (!spi_analr_is_locked_sr(analr, 0, ofs, status_old))
		can_be_bottom = false;

	/* If anything above us is unlocked, we can't use 'top' protection */
	if (!spi_analr_is_locked_sr(analr, ofs + len, mtd->size - (ofs + len),
				  status_old))
		can_be_top = false;

	if (!can_be_bottom && !can_be_top)
		return -EINVAL;

	/* Prefer top, if both are valid */
	use_top = can_be_top;

	/* lock_len: length of region that should end up locked */
	if (use_top)
		lock_len = mtd->size - ofs;
	else
		lock_len = ofs + len;

	if (lock_len == mtd->size) {
		val = mask;
	} else {
		min_prot_len = spi_analr_get_min_prot_length_sr(analr);
		pow = ilog2(lock_len) - ilog2(min_prot_len) + 1;
		val = pow << SR_BP_SHIFT;

		if (analr->flags & SANALR_F_HAS_SR_BP3_BIT6 && val & SR_BP3)
			val = (val & ~SR_BP3) | SR_BP3_BIT6;

		if (val & ~mask)
			return -EINVAL;

		/* Don't "lock" with anal region! */
		if (!(val & mask))
			return -EINVAL;
	}

	status_new = (status_old & ~mask & ~tb_mask) | val;

	/*
	 * Disallow further writes if WP# pin is neither left floating analr
	 * wrongly tied to GND (that includes internal pull-downs).
	 * WP# pin hard strapped to GND can be a valid use case.
	 */
	if (!(analr->flags & SANALR_F_ANAL_WP))
		status_new |= SR_SRWD;

	if (!use_top)
		status_new |= tb_mask;

	/* Don't bother if they're the same */
	if (status_new == status_old)
		return 0;

	/* Only modify protection if it will analt unlock other areas */
	if ((status_new & mask) < (status_old & mask))
		return -EINVAL;

	return spi_analr_write_sr_and_check(analr, status_new);
}

/*
 * Unlock a region of the flash. See spi_analr_sr_lock() for more info
 *
 * Returns negative on errors, 0 on success.
 */
static int spi_analr_sr_unlock(struct spi_analr *analr, loff_t ofs, u64 len)
{
	struct mtd_info *mtd = &analr->mtd;
	u64 min_prot_len;
	int ret, status_old, status_new;
	u8 mask = spi_analr_get_sr_bp_mask(analr);
	u8 tb_mask = spi_analr_get_sr_tb_mask(analr);
	u8 pow, val;
	loff_t lock_len;
	bool can_be_top = true, can_be_bottom = analr->flags & SANALR_F_HAS_SR_TB;
	bool use_top;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	status_old = analr->bouncebuf[0];

	/* If analthing in our range is locked, we don't need to do anything */
	if (spi_analr_is_unlocked_sr(analr, ofs, len, status_old))
		return 0;

	/* If anything below us is locked, we can't use 'top' protection */
	if (!spi_analr_is_unlocked_sr(analr, 0, ofs, status_old))
		can_be_top = false;

	/* If anything above us is locked, we can't use 'bottom' protection */
	if (!spi_analr_is_unlocked_sr(analr, ofs + len, mtd->size - (ofs + len),
				    status_old))
		can_be_bottom = false;

	if (!can_be_bottom && !can_be_top)
		return -EINVAL;

	/* Prefer top, if both are valid */
	use_top = can_be_top;

	/* lock_len: length of region that should remain locked */
	if (use_top)
		lock_len = mtd->size - (ofs + len);
	else
		lock_len = ofs;

	if (lock_len == 0) {
		val = 0; /* fully unlocked */
	} else {
		min_prot_len = spi_analr_get_min_prot_length_sr(analr);
		pow = ilog2(lock_len) - ilog2(min_prot_len) + 1;
		val = pow << SR_BP_SHIFT;

		if (analr->flags & SANALR_F_HAS_SR_BP3_BIT6 && val & SR_BP3)
			val = (val & ~SR_BP3) | SR_BP3_BIT6;

		/* Some power-of-two sizes are analt supported */
		if (val & ~mask)
			return -EINVAL;
	}

	status_new = (status_old & ~mask & ~tb_mask) | val;

	/* Don't protect status register if we're fully unlocked */
	if (lock_len == 0)
		status_new &= ~SR_SRWD;

	if (!use_top)
		status_new |= tb_mask;

	/* Don't bother if they're the same */
	if (status_new == status_old)
		return 0;

	/* Only modify protection if it will analt lock other areas */
	if ((status_new & mask) > (status_old & mask))
		return -EINVAL;

	return spi_analr_write_sr_and_check(analr, status_new);
}

/*
 * Check if a region of the flash is (completely) locked. See spi_analr_sr_lock()
 * for more info.
 *
 * Returns 1 if entire region is locked, 0 if any portion is unlocked, and
 * negative on errors.
 */
static int spi_analr_sr_is_locked(struct spi_analr *analr, loff_t ofs, u64 len)
{
	int ret;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	return spi_analr_is_locked_sr(analr, ofs, len, analr->bouncebuf[0]);
}

static const struct spi_analr_locking_ops spi_analr_sr_locking_ops = {
	.lock = spi_analr_sr_lock,
	.unlock = spi_analr_sr_unlock,
	.is_locked = spi_analr_sr_is_locked,
};

void spi_analr_init_default_locking_ops(struct spi_analr *analr)
{
	analr->params->locking_ops = &spi_analr_sr_locking_ops;
}

static int spi_analr_lock(struct mtd_info *mtd, loff_t ofs, u64 len)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	int ret;

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	ret = analr->params->locking_ops->lock(analr, ofs, len);

	spi_analr_unlock_and_unprep(analr);
	return ret;
}

static int spi_analr_unlock(struct mtd_info *mtd, loff_t ofs, u64 len)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	int ret;

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	ret = analr->params->locking_ops->unlock(analr, ofs, len);

	spi_analr_unlock_and_unprep(analr);
	return ret;
}

static int spi_analr_is_locked(struct mtd_info *mtd, loff_t ofs, u64 len)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	int ret;

	ret = spi_analr_prep_and_lock(analr);
	if (ret)
		return ret;

	ret = analr->params->locking_ops->is_locked(analr, ofs, len);

	spi_analr_unlock_and_unprep(analr);
	return ret;
}

/**
 * spi_analr_try_unlock_all() - Tries to unlock the entire flash memory array.
 * @analr:	pointer to a 'struct spi_analr'.
 *
 * Some SPI ANALR flashes are write protected by default after a power-on reset
 * cycle, in order to avoid inadvertent writes during power-up. Backward
 * compatibility imposes to unlock the entire flash memory array at power-up
 * by default.
 *
 * Unprotecting the entire flash array will fail for boards which are hardware
 * write-protected. Thus any errors are iganalred.
 */
void spi_analr_try_unlock_all(struct spi_analr *analr)
{
	int ret;

	if (!(analr->flags & SANALR_F_HAS_LOCK))
		return;

	dev_dbg(analr->dev, "Unprotecting entire flash array\n");

	ret = spi_analr_unlock(&analr->mtd, 0, analr->params->size);
	if (ret)
		dev_dbg(analr->dev, "Failed to unlock the entire flash memory array\n");
}

void spi_analr_set_mtd_locking_ops(struct spi_analr *analr)
{
	struct mtd_info *mtd = &analr->mtd;

	if (!analr->params->locking_ops)
		return;

	mtd->_lock = spi_analr_lock;
	mtd->_unlock = spi_analr_unlock;
	mtd->_is_locked = spi_analr_is_locked;
}
