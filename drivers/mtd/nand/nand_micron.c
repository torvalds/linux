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

static int micron_nand_init(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	ret = micron_nand_onfi_init(chip);
	if (ret)
		return ret;

	if (mtd->writesize == 2048)
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops micron_nand_manuf_ops = {
	.init = micron_nand_init,
};
