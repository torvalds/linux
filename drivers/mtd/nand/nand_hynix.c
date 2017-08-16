/*
 * Copyright (C) 2017 Free Electrons
 * Copyright (C) 2017 NextThing Co
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mtd/nand.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#define NAND_HYNIX_CMD_SET_PARAMS	0x36
#define NAND_HYNIX_CMD_APPLY_PARAMS	0x16

#define NAND_HYNIX_1XNM_RR_REPEAT	8

/**
 * struct hynix_read_retry - read-retry data
 * @nregs: number of register to set when applying a new read-retry mode
 * @regs: register offsets (NAND chip dependent)
 * @values: array of values to set in registers. The array size is equal to
 *	    (nregs * nmodes)
 */
struct hynix_read_retry {
	int nregs;
	const u8 *regs;
	u8 values[0];
};

/**
 * struct hynix_nand - private Hynix NAND struct
 * @nand_technology: manufacturing process expressed in picometer
 * @read_retry: read-retry information
 */
struct hynix_nand {
	const struct hynix_read_retry *read_retry;
};

/**
 * struct hynix_read_retry_otp - structure describing how the read-retry OTP
 *				 area
 * @nregs: number of hynix private registers to set before reading the reading
 *	   the OTP area
 * @regs: registers that should be configured
 * @values: values that should be set in regs
 * @page: the address to pass to the READ_PAGE command. Depends on the NAND
 *	  chip
 * @size: size of the read-retry OTP section
 */
struct hynix_read_retry_otp {
	int nregs;
	const u8 *regs;
	const u8 *values;
	int page;
	int size;
};

static bool hynix_nand_has_valid_jedecid(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 jedecid[6] = { };
	int i = 0;

	chip->cmdfunc(mtd, NAND_CMD_READID, 0x40, -1);
	for (i = 0; i < 5; i++)
		jedecid[i] = chip->read_byte(mtd);

	return !strcmp("JEDEC", jedecid);
}

static int hynix_nand_setup_read_retry(struct mtd_info *mtd, int retry_mode)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hynix_nand *hynix = nand_get_manufacturer_data(chip);
	const u8 *values;
	int status;
	int i;

	values = hynix->read_retry->values +
		 (retry_mode * hynix->read_retry->nregs);

	/* Enter 'Set Hynix Parameters' mode */
	chip->cmdfunc(mtd, NAND_HYNIX_CMD_SET_PARAMS, -1, -1);

	/*
	 * Configure the NAND in the requested read-retry mode.
	 * This is done by setting pre-defined values in internal NAND
	 * registers.
	 *
	 * The set of registers is NAND specific, and the values are either
	 * predefined or extracted from an OTP area on the NAND (values are
	 * probably tweaked at production in this case).
	 */
	for (i = 0; i < hynix->read_retry->nregs; i++) {
		int column = hynix->read_retry->regs[i];

		column |= column << 8;
		chip->cmdfunc(mtd, NAND_CMD_NONE, column, -1);
		chip->write_byte(mtd, values[i]);
	}

	/* Apply the new settings. */
	chip->cmdfunc(mtd, NAND_HYNIX_CMD_APPLY_PARAMS, -1, -1);

	status = chip->waitfunc(mtd, chip);
	if (status & NAND_STATUS_FAIL)
		return -EIO;

	return 0;
}

/**
 * hynix_get_majority - get the value that is occurring the most in a given
 *			set of values
 * @in: the array of values to test
 * @repeat: the size of the in array
 * @out: pointer used to store the output value
 *
 * This function implements the 'majority check' logic that is supposed to
 * overcome the unreliability of MLC NANDs when reading the OTP area storing
 * the read-retry parameters.
 *
 * It's based on a pretty simple assumption: if we repeat the same value
 * several times and then take the one that is occurring the most, we should
 * find the correct value.
 * Let's hope this dummy algorithm prevents us from losing the read-retry
 * parameters.
 */
static int hynix_get_majority(const u8 *in, int repeat, u8 *out)
{
	int i, j, half = repeat / 2;

	/*
	 * We only test the first half of the in array because we must ensure
	 * that the value is at least occurring repeat / 2 times.
	 *
	 * This loop is suboptimal since we may count the occurrences of the
	 * same value several time, but we are doing that on small sets, which
	 * makes it acceptable.
	 */
	for (i = 0; i < half; i++) {
		int cnt = 0;
		u8 val = in[i];

		/* Count all values that are matching the one at index i. */
		for (j = i + 1; j < repeat; j++) {
			if (in[j] == val)
				cnt++;
		}

		/* We found a value occurring more than repeat / 2. */
		if (cnt > half) {
			*out = val;
			return 0;
		}
	}

	return -EIO;
}

static int hynix_read_rr_otp(struct nand_chip *chip,
			     const struct hynix_read_retry_otp *info,
			     void *buf)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int i;

	chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);

	chip->cmdfunc(mtd, NAND_HYNIX_CMD_SET_PARAMS, -1, -1);

	for (i = 0; i < info->nregs; i++) {
		int column = info->regs[i];

		column |= column << 8;
		chip->cmdfunc(mtd, NAND_CMD_NONE, column, -1);
		chip->write_byte(mtd, info->values[i]);
	}

	chip->cmdfunc(mtd, NAND_HYNIX_CMD_APPLY_PARAMS, -1, -1);

	/* Sequence to enter OTP mode? */
	chip->cmdfunc(mtd, 0x17, -1, -1);
	chip->cmdfunc(mtd, 0x04, -1, -1);
	chip->cmdfunc(mtd, 0x19, -1, -1);

	/* Now read the page */
	chip->cmdfunc(mtd, NAND_CMD_READ0, 0x0, info->page);
	chip->read_buf(mtd, buf, info->size);

	/* Put everything back to normal */
	chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);
	chip->cmdfunc(mtd, NAND_HYNIX_CMD_SET_PARAMS, 0x38, -1);
	chip->write_byte(mtd, 0x0);
	chip->cmdfunc(mtd, NAND_HYNIX_CMD_APPLY_PARAMS, -1, -1);
	chip->cmdfunc(mtd, NAND_CMD_READ0, 0x0, -1);

	return 0;
}

#define NAND_HYNIX_1XNM_RR_COUNT_OFFS				0
#define NAND_HYNIX_1XNM_RR_REG_COUNT_OFFS			8
#define NAND_HYNIX_1XNM_RR_SET_OFFS(x, setsize, inv)		\
	(16 + ((((x) * 2) + ((inv) ? 1 : 0)) * (setsize)))

static int hynix_mlc_1xnm_rr_value(const u8 *buf, int nmodes, int nregs,
				   int mode, int reg, bool inv, u8 *val)
{
	u8 tmp[NAND_HYNIX_1XNM_RR_REPEAT];
	int val_offs = (mode * nregs) + reg;
	int set_size = nmodes * nregs;
	int i, ret;

	for (i = 0; i < NAND_HYNIX_1XNM_RR_REPEAT; i++) {
		int set_offs = NAND_HYNIX_1XNM_RR_SET_OFFS(i, set_size, inv);

		tmp[i] = buf[val_offs + set_offs];
	}

	ret = hynix_get_majority(tmp, NAND_HYNIX_1XNM_RR_REPEAT, val);
	if (ret)
		return ret;

	if (inv)
		*val = ~*val;

	return 0;
}

static u8 hynix_1xnm_mlc_read_retry_regs[] = {
	0xcc, 0xbf, 0xaa, 0xab, 0xcd, 0xad, 0xae, 0xaf
};

static int hynix_mlc_1xnm_rr_init(struct nand_chip *chip,
				  const struct hynix_read_retry_otp *info)
{
	struct hynix_nand *hynix = nand_get_manufacturer_data(chip);
	struct hynix_read_retry *rr = NULL;
	int ret, i, j;
	u8 nregs, nmodes;
	u8 *buf;

	buf = kmalloc(info->size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hynix_read_rr_otp(chip, info, buf);
	if (ret)
		goto out;

	ret = hynix_get_majority(buf, NAND_HYNIX_1XNM_RR_REPEAT,
				 &nmodes);
	if (ret)
		goto out;

	ret = hynix_get_majority(buf + NAND_HYNIX_1XNM_RR_REPEAT,
				 NAND_HYNIX_1XNM_RR_REPEAT,
				 &nregs);
	if (ret)
		goto out;

	rr = kzalloc(sizeof(*rr) + (nregs * nmodes), GFP_KERNEL);
	if (!rr) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < nmodes; i++) {
		for (j = 0; j < nregs; j++) {
			u8 *val = rr->values + (i * nregs);

			ret = hynix_mlc_1xnm_rr_value(buf, nmodes, nregs, i, j,
						      false, val);
			if (!ret)
				continue;

			ret = hynix_mlc_1xnm_rr_value(buf, nmodes, nregs, i, j,
						      true, val);
			if (ret)
				goto out;
		}
	}

	rr->nregs = nregs;
	rr->regs = hynix_1xnm_mlc_read_retry_regs;
	hynix->read_retry = rr;
	chip->setup_read_retry = hynix_nand_setup_read_retry;
	chip->read_retries = nmodes;

out:
	kfree(buf);

	if (ret)
		kfree(rr);

	return ret;
}

static const u8 hynix_mlc_1xnm_rr_otp_regs[] = { 0x38 };
static const u8 hynix_mlc_1xnm_rr_otp_values[] = { 0x52 };

static const struct hynix_read_retry_otp hynix_mlc_1xnm_rr_otps[] = {
	{
		.nregs = ARRAY_SIZE(hynix_mlc_1xnm_rr_otp_regs),
		.regs = hynix_mlc_1xnm_rr_otp_regs,
		.values = hynix_mlc_1xnm_rr_otp_values,
		.page = 0x21f,
		.size = 784
	},
	{
		.nregs = ARRAY_SIZE(hynix_mlc_1xnm_rr_otp_regs),
		.regs = hynix_mlc_1xnm_rr_otp_regs,
		.values = hynix_mlc_1xnm_rr_otp_values,
		.page = 0x200,
		.size = 528,
	},
};

static int hynix_nand_rr_init(struct nand_chip *chip)
{
	int i, ret = 0;
	bool valid_jedecid;

	valid_jedecid = hynix_nand_has_valid_jedecid(chip);

	/*
	 * We only support read-retry for 1xnm NANDs, and those NANDs all
	 * expose a valid JEDEC ID.
	 */
	if (valid_jedecid) {
		u8 nand_tech = chip->id.data[5] >> 4;

		/* 1xnm technology */
		if (nand_tech == 4) {
			for (i = 0; i < ARRAY_SIZE(hynix_mlc_1xnm_rr_otps);
			     i++) {
				/*
				 * FIXME: Hynix recommend to copy the
				 * read-retry OTP area into a normal page.
				 */
				ret = hynix_mlc_1xnm_rr_init(chip,
						hynix_mlc_1xnm_rr_otps);
				if (!ret)
					break;
			}
		}
	}

	if (ret)
		pr_warn("failed to initialize read-retry infrastructure");

	return 0;
}

static void hynix_nand_extract_oobsize(struct nand_chip *chip,
				       bool valid_jedecid)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 oobsize;

	oobsize = ((chip->id.data[3] >> 2) & 0x3) |
		  ((chip->id.data[3] >> 4) & 0x4);

	if (valid_jedecid) {
		switch (oobsize) {
		case 0:
			mtd->oobsize = 2048;
			break;
		case 1:
			mtd->oobsize = 1664;
			break;
		case 2:
			mtd->oobsize = 1024;
			break;
		case 3:
			mtd->oobsize = 640;
			break;
		default:
			/*
			 * We should never reach this case, but if that
			 * happens, this probably means Hynix decided to use
			 * a different extended ID format, and we should find
			 * a way to support it.
			 */
			WARN(1, "Invalid OOB size");
			break;
		}
	} else {
		switch (oobsize) {
		case 0:
			mtd->oobsize = 128;
			break;
		case 1:
			mtd->oobsize = 224;
			break;
		case 2:
			mtd->oobsize = 448;
			break;
		case 3:
			mtd->oobsize = 64;
			break;
		case 4:
			mtd->oobsize = 32;
			break;
		case 5:
			mtd->oobsize = 16;
			break;
		case 6:
			mtd->oobsize = 640;
			break;
		default:
			/*
			 * We should never reach this case, but if that
			 * happens, this probably means Hynix decided to use
			 * a different extended ID format, and we should find
			 * a way to support it.
			 */
			WARN(1, "Invalid OOB size");
			break;
		}
	}
}

static void hynix_nand_extract_ecc_requirements(struct nand_chip *chip,
						bool valid_jedecid)
{
	u8 ecc_level = (chip->id.data[4] >> 4) & 0x7;

	if (valid_jedecid) {
		/* Reference: H27UCG8T2E datasheet */
		chip->ecc_step_ds = 1024;

		switch (ecc_level) {
		case 0:
			chip->ecc_step_ds = 0;
			chip->ecc_strength_ds = 0;
			break;
		case 1:
			chip->ecc_strength_ds = 4;
			break;
		case 2:
			chip->ecc_strength_ds = 24;
			break;
		case 3:
			chip->ecc_strength_ds = 32;
			break;
		case 4:
			chip->ecc_strength_ds = 40;
			break;
		case 5:
			chip->ecc_strength_ds = 50;
			break;
		case 6:
			chip->ecc_strength_ds = 60;
			break;
		default:
			/*
			 * We should never reach this case, but if that
			 * happens, this probably means Hynix decided to use
			 * a different extended ID format, and we should find
			 * a way to support it.
			 */
			WARN(1, "Invalid ECC requirements");
		}
	} else {
		/*
		 * The ECC requirements field meaning depends on the
		 * NAND technology.
		 */
		u8 nand_tech = chip->id.data[5] & 0x3;

		if (nand_tech < 3) {
			/* > 26nm, reference: H27UBG8T2A datasheet */
			if (ecc_level < 5) {
				chip->ecc_step_ds = 512;
				chip->ecc_strength_ds = 1 << ecc_level;
			} else if (ecc_level < 7) {
				if (ecc_level == 5)
					chip->ecc_step_ds = 2048;
				else
					chip->ecc_step_ds = 1024;
				chip->ecc_strength_ds = 24;
			} else {
				/*
				 * We should never reach this case, but if that
				 * happens, this probably means Hynix decided
				 * to use a different extended ID format, and
				 * we should find a way to support it.
				 */
				WARN(1, "Invalid ECC requirements");
			}
		} else {
			/* <= 26nm, reference: H27UBG8T2B datasheet */
			if (!ecc_level) {
				chip->ecc_step_ds = 0;
				chip->ecc_strength_ds = 0;
			} else if (ecc_level < 5) {
				chip->ecc_step_ds = 512;
				chip->ecc_strength_ds = 1 << (ecc_level - 1);
			} else {
				chip->ecc_step_ds = 1024;
				chip->ecc_strength_ds = 24 +
							(8 * (ecc_level - 5));
			}
		}
	}
}

static void hynix_nand_extract_scrambling_requirements(struct nand_chip *chip,
						       bool valid_jedecid)
{
	u8 nand_tech;

	/* We need scrambling on all TLC NANDs*/
	if (chip->bits_per_cell > 2)
		chip->options |= NAND_NEED_SCRAMBLING;

	/* And on MLC NANDs with sub-3xnm process */
	if (valid_jedecid) {
		nand_tech = chip->id.data[5] >> 4;

		/* < 3xnm */
		if (nand_tech > 0)
			chip->options |= NAND_NEED_SCRAMBLING;
	} else {
		nand_tech = chip->id.data[5] & 0x3;

		/* < 32nm */
		if (nand_tech > 2)
			chip->options |= NAND_NEED_SCRAMBLING;
	}
}

static void hynix_nand_decode_id(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	bool valid_jedecid;
	u8 tmp;

	/*
	 * Exclude all SLC NANDs from this advanced detection scheme.
	 * According to the ranges defined in several datasheets, it might
	 * appear that even SLC NANDs could fall in this extended ID scheme.
	 * If that the case rework the test to let SLC NANDs go through the
	 * detection process.
	 */
	if (chip->id.len < 6 || nand_is_slc(chip)) {
		nand_decode_ext_id(chip);
		return;
	}

	/* Extract pagesize */
	mtd->writesize = 2048 << (chip->id.data[3] & 0x03);

	tmp = (chip->id.data[3] >> 4) & 0x3;
	/*
	 * When bit7 is set that means we start counting at 1MiB, otherwise
	 * we start counting at 128KiB and shift this value the content of
	 * ID[3][4:5].
	 * The only exception is when ID[3][4:5] == 3 and ID[3][7] == 0, in
	 * this case the erasesize is set to 768KiB.
	 */
	if (chip->id.data[3] & 0x80)
		mtd->erasesize = SZ_1M << tmp;
	else if (tmp == 3)
		mtd->erasesize = SZ_512K + SZ_256K;
	else
		mtd->erasesize = SZ_128K << tmp;

	/*
	 * Modern Toggle DDR NANDs have a valid JEDECID even though they are
	 * not exposing a valid JEDEC parameter table.
	 * These NANDs use a different NAND ID scheme.
	 */
	valid_jedecid = hynix_nand_has_valid_jedecid(chip);

	hynix_nand_extract_oobsize(chip, valid_jedecid);
	hynix_nand_extract_ecc_requirements(chip, valid_jedecid);
	hynix_nand_extract_scrambling_requirements(chip, valid_jedecid);
}

static void hynix_nand_cleanup(struct nand_chip *chip)
{
	struct hynix_nand *hynix = nand_get_manufacturer_data(chip);

	if (!hynix)
		return;

	kfree(hynix->read_retry);
	kfree(hynix);
	nand_set_manufacturer_data(chip, NULL);
}

static int hynix_nand_init(struct nand_chip *chip)
{
	struct hynix_nand *hynix;
	int ret;

	if (!nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCANLASTPAGE;
	else
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	hynix = kzalloc(sizeof(*hynix), GFP_KERNEL);
	if (!hynix)
		return -ENOMEM;

	nand_set_manufacturer_data(chip, hynix);

	ret = hynix_nand_rr_init(chip);
	if (ret)
		hynix_nand_cleanup(chip);

	return ret;
}

const struct nand_manufacturer_ops hynix_nand_manuf_ops = {
	.detect = hynix_nand_decode_id,
	.init = hynix_nand_init,
	.cleanup = hynix_nand_cleanup,
};
