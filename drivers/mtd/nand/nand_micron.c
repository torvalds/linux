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

#include <linux/mtd/rawnand.h>

/*
 * Special Micron status bit that indicates when the block has been
 * corrected by on-die ECC and should be rewritten
 */
#define NAND_STATUS_WRITE_RECOMMENDED	BIT(3)

struct nand_onfi_vendor_micron {
	u8 two_plane_read;
	u8 read_cache;
	u8 read_unique_id;
	u8 dq_imped;
	u8 dq_imped_num_settings;
	u8 dq_imped_feat_addr;
	u8 rb_pulldown_strength;
	u8 rb_pulldown_strength_feat_addr;
	u8 rb_pulldown_strength_num_settings;
	u8 otp_mode;
	u8 otp_page_start;
	u8 otp_data_prot_addr;
	u8 otp_num_pages;
	u8 otp_feat_addr;
	u8 read_retry_options;
	u8 reserved[72];
	u8 param_revision;
} __packed;

static int micron_nand_setup_read_retry(struct mtd_info *mtd, int retry_mode)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN] = {retry_mode};

	return chip->onfi_set_features(mtd, chip, ONFI_FEATURE_ADDR_READ_RETRY,
				       feature);
}

/*
 * Configure chip properties from Micron vendor-specific ONFI table
 */
static int micron_nand_onfi_init(struct nand_chip *chip)
{
	struct nand_onfi_params *p = &chip->onfi_params;
	struct nand_onfi_vendor_micron *micron = (void *)p->vendor;

	if (!chip->onfi_version)
		return 0;

	if (le16_to_cpu(p->vendor_revision) < 1)
		return 0;

	chip->read_retries = micron->read_retry_options;
	chip->setup_read_retry = micron_nand_setup_read_retry;

	return 0;
}

static int micron_nand_on_die_ooblayout_ecc(struct mtd_info *mtd, int section,
					    struct mtd_oob_region *oobregion)
{
	if (section >= 4)
		return -ERANGE;

	oobregion->offset = (section * 16) + 8;
	oobregion->length = 8;

	return 0;
}

static int micron_nand_on_die_ooblayout_free(struct mtd_info *mtd, int section,
					     struct mtd_oob_region *oobregion)
{
	if (section >= 4)
		return -ERANGE;

	oobregion->offset = (section * 16) + 2;
	oobregion->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops micron_nand_on_die_ooblayout_ops = {
	.ecc = micron_nand_on_die_ooblayout_ecc,
	.free = micron_nand_on_die_ooblayout_free,
};

static int micron_nand_on_die_ecc_setup(struct nand_chip *chip, bool enable)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN] = { 0, };

	if (enable)
		feature[0] |= ONFI_FEATURE_ON_DIE_ECC_EN;

	return chip->onfi_set_features(nand_to_mtd(chip), chip,
				       ONFI_FEATURE_ON_DIE_ECC, feature);
}

static int
micron_nand_read_page_on_die_ecc(struct mtd_info *mtd, struct nand_chip *chip,
				 uint8_t *buf, int oob_required,
				 int page)
{
	int status;
	int max_bitflips = 0;

	micron_nand_on_die_ecc_setup(chip, true);

	chip->cmdfunc(mtd, NAND_CMD_READ0, 0x00, page);
	chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);
	status = chip->read_byte(mtd);
	if (status & NAND_STATUS_FAIL)
		mtd->ecc_stats.failed++;
	/*
	 * The internal ECC doesn't tell us the number of bitflips
	 * that have been corrected, but tells us if it recommends to
	 * rewrite the block. If it's the case, then we pretend we had
	 * a number of bitflips equal to the ECC strength, which will
	 * hint the NAND core to rewrite the block.
	 */
	else if (status & NAND_STATUS_WRITE_RECOMMENDED)
		max_bitflips = chip->ecc.strength;

	chip->cmdfunc(mtd, NAND_CMD_READ0, -1, -1);

	nand_read_page_raw(mtd, chip, buf, oob_required, page);

	micron_nand_on_die_ecc_setup(chip, false);

	return max_bitflips;
}

static int
micron_nand_write_page_on_die_ecc(struct mtd_info *mtd, struct nand_chip *chip,
				  const uint8_t *buf, int oob_required,
				  int page)
{
	int status;

	micron_nand_on_die_ecc_setup(chip, true);

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, page);
	nand_write_page_raw(mtd, chip, buf, oob_required, page);
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	micron_nand_on_die_ecc_setup(chip, false);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

static int
micron_nand_read_page_raw_on_die_ecc(struct mtd_info *mtd,
				     struct nand_chip *chip,
				     uint8_t *buf, int oob_required,
				     int page)
{
	chip->cmdfunc(mtd, NAND_CMD_READ0, 0x00, page);
	nand_read_page_raw(mtd, chip, buf, oob_required, page);

	return 0;
}

static int
micron_nand_write_page_raw_on_die_ecc(struct mtd_info *mtd,
				      struct nand_chip *chip,
				      const uint8_t *buf, int oob_required,
				      int page)
{
	int status;

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, page);
	nand_write_page_raw(mtd, chip, buf, oob_required, page);
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

enum {
	/* The NAND flash doesn't support on-die ECC */
	MICRON_ON_DIE_UNSUPPORTED,

	/*
	 * The NAND flash supports on-die ECC and it can be
	 * enabled/disabled by a set features command.
	 */
	MICRON_ON_DIE_SUPPORTED,

	/*
	 * The NAND flash supports on-die ECC, and it cannot be
	 * disabled.
	 */
	MICRON_ON_DIE_MANDATORY,
};

/*
 * Try to detect if the NAND support on-die ECC. To do this, we enable
 * the feature, and read back if it has been enabled as expected. We
 * also check if it can be disabled, because some Micron NANDs do not
 * allow disabling the on-die ECC and we don't support such NANDs for
 * now.
 *
 * This function also has the side effect of disabling on-die ECC if
 * it had been left enabled by the firmware/bootloader.
 */
static int micron_supports_on_die_ecc(struct nand_chip *chip)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN] = { 0, };
	int ret;

	if (chip->onfi_version == 0)
		return MICRON_ON_DIE_UNSUPPORTED;

	if (chip->bits_per_cell != 1)
		return MICRON_ON_DIE_UNSUPPORTED;

	ret = micron_nand_on_die_ecc_setup(chip, true);
	if (ret)
		return MICRON_ON_DIE_UNSUPPORTED;

	chip->onfi_get_features(nand_to_mtd(chip), chip,
				ONFI_FEATURE_ON_DIE_ECC, feature);
	if ((feature[0] & ONFI_FEATURE_ON_DIE_ECC_EN) == 0)
		return MICRON_ON_DIE_UNSUPPORTED;

	ret = micron_nand_on_die_ecc_setup(chip, false);
	if (ret)
		return MICRON_ON_DIE_UNSUPPORTED;

	chip->onfi_get_features(nand_to_mtd(chip), chip,
				ONFI_FEATURE_ON_DIE_ECC, feature);
	if (feature[0] & ONFI_FEATURE_ON_DIE_ECC_EN)
		return MICRON_ON_DIE_MANDATORY;

	/*
	 * Some Micron NANDs have an on-die ECC of 4/512, some other
	 * 8/512. We only support the former.
	 */
	if (chip->onfi_params.ecc_bits != 4)
		return MICRON_ON_DIE_UNSUPPORTED;

	return MICRON_ON_DIE_SUPPORTED;
}

static int micron_nand_init(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ondie;
	int ret;

	ret = micron_nand_onfi_init(chip);
	if (ret)
		return ret;

	if (mtd->writesize == 2048)
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	ondie = micron_supports_on_die_ecc(chip);

	if (ondie == MICRON_ON_DIE_MANDATORY) {
		pr_err("On-die ECC forcefully enabled, not supported\n");
		return -EINVAL;
	}

	if (chip->ecc.mode == NAND_ECC_ON_DIE) {
		if (ondie == MICRON_ON_DIE_UNSUPPORTED) {
			pr_err("On-die ECC selected but not supported\n");
			return -EINVAL;
		}

		chip->ecc.options = NAND_ECC_CUSTOM_PAGE_ACCESS;
		chip->ecc.bytes = 8;
		chip->ecc.size = 512;
		chip->ecc.strength = 4;
		chip->ecc.algo = NAND_ECC_BCH;
		chip->ecc.read_page = micron_nand_read_page_on_die_ecc;
		chip->ecc.write_page = micron_nand_write_page_on_die_ecc;
		chip->ecc.read_page_raw =
			micron_nand_read_page_raw_on_die_ecc;
		chip->ecc.write_page_raw =
			micron_nand_write_page_raw_on_die_ecc;

		mtd_set_ooblayout(mtd, &micron_nand_on_die_ooblayout_ops);
	}

	return 0;
}

const struct nand_manufacturer_ops micron_nand_manuf_ops = {
	.init = micron_nand_init,
};
