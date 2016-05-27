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

static int hynix_nand_init(struct nand_chip *chip)
{
	if (!nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCANLASTPAGE;
	else
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops hynix_nand_manuf_ops = {
	.detect = hynix_nand_decode_id,
	.init = hynix_nand_init,
};
