// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Free Electrons
 * Copyright (C) 2017 NextThing Co
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 */

#include "internals.h"

/* Bit for detecting BENAND */
#define TOSHIBA_NAND_ID4_IS_BENAND		BIT(7)

/* Recommended to rewrite for BENAND */
#define TOSHIBA_NAND_STATUS_REWRITE_RECOMMENDED	BIT(3)

/* ECC Status Read Command for BENAND */
#define TOSHIBA_NAND_CMD_ECC_STATUS_READ	0x7A

/* ECC Status Mask for BENAND */
#define TOSHIBA_NAND_ECC_STATUS_MASK		0x0F

/* Uncorrectable Error for BENAND */
#define TOSHIBA_NAND_ECC_STATUS_UNCORR		0x0F

/* Max ECC Steps for BENAND */
#define TOSHIBA_NAND_MAX_ECC_STEPS		8

static int toshiba_nand_benand_read_eccstatus_op(struct nand_chip *chip,
						 u8 *buf)
{
	u8 *ecc_status = buf;

	if (nand_has_exec_op(chip)) {
		const struct nand_sdr_timings *sdr =
			nand_get_sdr_timings(&chip->data_interface);
		struct nand_op_instr instrs[] = {
			NAND_OP_CMD(TOSHIBA_NAND_CMD_ECC_STATUS_READ,
				    PSEC_TO_NSEC(sdr->tADL_min)),
			NAND_OP_8BIT_DATA_IN(chip->ecc.steps, ecc_status, 0),
		};
		struct nand_operation op = NAND_OPERATION(chip->cur_cs, instrs);

		return nand_exec_op(chip, &op);
	}

	return -ENOTSUPP;
}

static int toshiba_nand_benand_eccstatus(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;
	unsigned int max_bitflips = 0;
	u8 status, ecc_status[TOSHIBA_NAND_MAX_ECC_STEPS];

	/* Check Status */
	ret = toshiba_nand_benand_read_eccstatus_op(chip, ecc_status);
	if (!ret) {
		unsigned int i, bitflips = 0;

		for (i = 0; i < chip->ecc.steps; i++) {
			bitflips = ecc_status[i] & TOSHIBA_NAND_ECC_STATUS_MASK;
			if (bitflips == TOSHIBA_NAND_ECC_STATUS_UNCORR) {
				mtd->ecc_stats.failed++;
			} else {
				mtd->ecc_stats.corrected += bitflips;
				max_bitflips = max(max_bitflips, bitflips);
			}
		}

		return max_bitflips;
	}

	/*
	 * Fallback to regular status check if
	 * toshiba_nand_benand_read_eccstatus_op() failed.
	 */
	ret = nand_status_op(chip, &status);
	if (ret)
		return ret;

	if (status & NAND_STATUS_FAIL) {
		/* uncorrected */
		mtd->ecc_stats.failed++;
	} else if (status & TOSHIBA_NAND_STATUS_REWRITE_RECOMMENDED) {
		/* corrected */
		max_bitflips = mtd->bitflip_threshold;
		mtd->ecc_stats.corrected += max_bitflips;
	}

	return max_bitflips;
}

static int
toshiba_nand_read_page_benand(struct nand_chip *chip, uint8_t *buf,
			      int oob_required, int page)
{
	int ret;

	ret = nand_read_page_raw(chip, buf, oob_required, page);
	if (ret)
		return ret;

	return toshiba_nand_benand_eccstatus(chip);
}

static int
toshiba_nand_read_subpage_benand(struct nand_chip *chip, uint32_t data_offs,
				 uint32_t readlen, uint8_t *bufpoi, int page)
{
	int ret;

	ret = nand_read_page_op(chip, page, data_offs,
				bufpoi + data_offs, readlen);
	if (ret)
		return ret;

	return toshiba_nand_benand_eccstatus(chip);
}

static void toshiba_nand_benand_init(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/*
	 * On BENAND, the entire OOB region can be used by the MTD user.
	 * The calculated ECC bytes are stored into other isolated
	 * area which is not accessible to users.
	 * This is why chip->ecc.bytes = 0.
	 */
	chip->ecc.bytes = 0;
	chip->ecc.size = 512;
	chip->ecc.strength = 8;
	chip->ecc.read_page = toshiba_nand_read_page_benand;
	chip->ecc.read_subpage = toshiba_nand_read_subpage_benand;
	chip->ecc.write_page = nand_write_page_raw;
	chip->ecc.read_page_raw = nand_read_page_raw_notsupp;
	chip->ecc.write_page_raw = nand_write_page_raw_notsupp;

	chip->options |= NAND_SUBPAGE_READ;

	mtd_set_ooblayout(mtd, &nand_ooblayout_lp_ops);
}

static void toshiba_nand_decode_id(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_memory_organization *memorg;

	memorg = nanddev_get_memorg(&chip->base);

	nand_decode_ext_id(chip);

	/*
	 * Toshiba 24nm raw SLC (i.e., not BENAND) have 32B OOB per
	 * 512B page. For Toshiba SLC, we decode the 5th/6th byte as
	 * follows:
	 * - ID byte 6, bits[2:0]: 100b -> 43nm, 101b -> 32nm,
	 *                         110b -> 24nm
	 * - ID byte 5, bit[7]:    1 -> BENAND, 0 -> raw SLC
	 */
	if (chip->id.len >= 6 && nand_is_slc(chip) &&
	    (chip->id.data[5] & 0x7) == 0x6 /* 24nm */ &&
	    !(chip->id.data[4] & TOSHIBA_NAND_ID4_IS_BENAND) /* !BENAND */) {
		memorg->oobsize = 32 * memorg->pagesize >> 9;
		mtd->oobsize = memorg->oobsize;
	}

	/*
	 * Extract ECC requirements from 6th id byte.
	 * For Toshiba SLC, ecc requrements are as follows:
	 *  - 43nm: 1 bit ECC for each 512Byte is required.
	 *  - 32nm: 4 bit ECC for each 512Byte is required.
	 *  - 24nm: 8 bit ECC for each 512Byte is required.
	 */
	if (chip->id.len >= 6 && nand_is_slc(chip)) {
		chip->base.eccreq.step_size = 512;
		switch (chip->id.data[5] & 0x7) {
		case 0x4:
			chip->base.eccreq.strength = 1;
			break;
		case 0x5:
			chip->base.eccreq.strength = 4;
			break;
		case 0x6:
			chip->base.eccreq.strength = 8;
			break;
		default:
			WARN(1, "Could not get ECC info");
			chip->base.eccreq.step_size = 0;
			break;
		}
	}
}

static int tc58teg5dclta00_init(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	chip->onfi_timing_mode_default = 5;
	chip->options |= NAND_NEED_SCRAMBLING;
	mtd_set_pairing_scheme(mtd, &dist3_pairing_scheme);

	return 0;
}

static int toshiba_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->options |= NAND_BBM_FIRSTPAGE | NAND_BBM_SECONDPAGE;

	/* Check that chip is BENAND and ECC mode is on-die */
	if (nand_is_slc(chip) && chip->ecc.mode == NAND_ECC_ON_DIE &&
	    chip->id.data[4] & TOSHIBA_NAND_ID4_IS_BENAND)
		toshiba_nand_benand_init(chip);

	if (!strcmp("TC58TEG5DCLTA00", chip->parameters.model))
		tc58teg5dclta00_init(chip);

	return 0;
}

const struct nand_manufacturer_ops toshiba_nand_manuf_ops = {
	.detect = toshiba_nand_decode_id,
	.init = toshiba_nand_init,
};
