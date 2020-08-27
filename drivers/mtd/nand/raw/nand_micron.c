// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Free Electrons
 * Copyright (C) 2017 NextThing Co
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 */

#include <linux/slab.h>

#include "internals.h"

/*
 * Special Micron status bit 3 indicates that the block has been
 * corrected by on-die ECC and should be rewritten.
 */
#define NAND_ECC_STATUS_WRITE_RECOMMENDED	BIT(3)

/*
 * On chips with 8-bit ECC and additional bit can be used to distinguish
 * cases where a errors were corrected without needing a rewrite
 *
 * Bit 4 Bit 3 Bit 0 Description
 * ----- ----- ----- -----------
 * 0     0     0     No Errors
 * 0     0     1     Multiple uncorrected errors
 * 0     1     0     4 - 6 errors corrected, recommend rewrite
 * 0     1     1     Reserved
 * 1     0     0     1 - 3 errors corrected
 * 1     0     1     Reserved
 * 1     1     0     7 - 8 errors corrected, recommend rewrite
 */
#define NAND_ECC_STATUS_MASK		(BIT(4) | BIT(3) | BIT(0))
#define NAND_ECC_STATUS_UNCORRECTABLE	BIT(0)
#define NAND_ECC_STATUS_4_6_CORRECTED	BIT(3)
#define NAND_ECC_STATUS_1_3_CORRECTED	BIT(4)
#define NAND_ECC_STATUS_7_8_CORRECTED	(BIT(4) | BIT(3))

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

struct micron_on_die_ecc {
	bool forced;
	bool enabled;
	void *rawbuf;
};

struct micron_nand {
	struct micron_on_die_ecc ecc;
};

static int micron_nand_setup_read_retry(struct nand_chip *chip, int retry_mode)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN] = {retry_mode};

	return nand_set_features(chip, ONFI_FEATURE_ADDR_READ_RETRY, feature);
}

/*
 * Configure chip properties from Micron vendor-specific ONFI table
 */
static int micron_nand_onfi_init(struct nand_chip *chip)
{
	struct nand_parameters *p = &chip->parameters;

	if (p->onfi) {
		struct nand_onfi_vendor_micron *micron = (void *)p->onfi->vendor;

		chip->read_retries = micron->read_retry_options;
		chip->ops.setup_read_retry = micron_nand_setup_read_retry;
	}

	if (p->supports_set_get_features) {
		set_bit(ONFI_FEATURE_ADDR_READ_RETRY, p->set_feature_list);
		set_bit(ONFI_FEATURE_ON_DIE_ECC, p->set_feature_list);
		set_bit(ONFI_FEATURE_ADDR_READ_RETRY, p->get_feature_list);
		set_bit(ONFI_FEATURE_ON_DIE_ECC, p->get_feature_list);
	}

	return 0;
}

static int micron_nand_on_die_4_ooblayout_ecc(struct mtd_info *mtd,
					      int section,
					      struct mtd_oob_region *oobregion)
{
	if (section >= 4)
		return -ERANGE;

	oobregion->offset = (section * 16) + 8;
	oobregion->length = 8;

	return 0;
}

static int micron_nand_on_die_4_ooblayout_free(struct mtd_info *mtd,
					       int section,
					       struct mtd_oob_region *oobregion)
{
	if (section >= 4)
		return -ERANGE;

	oobregion->offset = (section * 16) + 2;
	oobregion->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops micron_nand_on_die_4_ooblayout_ops = {
	.ecc = micron_nand_on_die_4_ooblayout_ecc,
	.free = micron_nand_on_die_4_ooblayout_free,
};

static int micron_nand_on_die_8_ooblayout_ecc(struct mtd_info *mtd,
					      int section,
					      struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = mtd->oobsize - chip->ecc.total;
	oobregion->length = chip->ecc.total;

	return 0;
}

static int micron_nand_on_die_8_ooblayout_free(struct mtd_info *mtd,
					       int section,
					       struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = 2;
	oobregion->length = mtd->oobsize - chip->ecc.total - 2;

	return 0;
}

static const struct mtd_ooblayout_ops micron_nand_on_die_8_ooblayout_ops = {
	.ecc = micron_nand_on_die_8_ooblayout_ecc,
	.free = micron_nand_on_die_8_ooblayout_free,
};

static int micron_nand_on_die_ecc_setup(struct nand_chip *chip, bool enable)
{
	struct micron_nand *micron = nand_get_manufacturer_data(chip);
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN] = { 0, };
	int ret;

	if (micron->ecc.forced)
		return 0;

	if (micron->ecc.enabled == enable)
		return 0;

	if (enable)
		feature[0] |= ONFI_FEATURE_ON_DIE_ECC_EN;

	ret = nand_set_features(chip, ONFI_FEATURE_ON_DIE_ECC, feature);
	if (!ret)
		micron->ecc.enabled = enable;

	return ret;
}

static int micron_nand_on_die_ecc_status_4(struct nand_chip *chip, u8 status,
					   void *buf, int page,
					   int oob_required)
{
	struct micron_nand *micron = nand_get_manufacturer_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int step, max_bitflips = 0;
	bool use_datain = false;
	int ret;

	if (!(status & NAND_ECC_STATUS_WRITE_RECOMMENDED)) {
		if (status & NAND_STATUS_FAIL)
			mtd->ecc_stats.failed++;

		return 0;
	}

	/*
	 * The internal ECC doesn't tell us the number of bitflips that have
	 * been corrected, but tells us if it recommends to rewrite the block.
	 * If it's the case, we need to read the page in raw mode and compare
	 * its content to the corrected version to extract the actual number of
	 * bitflips.
	 * But before we do that, we must make sure we have all OOB bytes read
	 * in non-raw mode, even if the user did not request those bytes.
	 */
	if (!oob_required) {
		/*
		 * We first check which operation is supported by the controller
		 * before running it. This trick makes it possible to support
		 * all controllers, even the most constraints, without almost
		 * any performance hit.
		 *
		 * TODO: could be enhanced to avoid repeating the same check
		 * over and over in the fast path.
		 */
		if (!nand_has_exec_op(chip) ||
		    !nand_read_data_op(chip, chip->oob_poi, mtd->oobsize, false,
				       true))
			use_datain = true;

		if (use_datain)
			ret = nand_read_data_op(chip, chip->oob_poi,
						mtd->oobsize, false, false);
		else
			ret = nand_change_read_column_op(chip, mtd->writesize,
							 chip->oob_poi,
							 mtd->oobsize, false);
		if (ret)
			return ret;
	}

	micron_nand_on_die_ecc_setup(chip, false);

	ret = nand_read_page_op(chip, page, 0, micron->ecc.rawbuf,
				mtd->writesize + mtd->oobsize);
	if (ret)
		return ret;

	for (step = 0; step < chip->ecc.steps; step++) {
		unsigned int offs, i, nbitflips = 0;
		u8 *rawbuf, *corrbuf;

		offs = step * chip->ecc.size;
		rawbuf = micron->ecc.rawbuf + offs;
		corrbuf = buf + offs;

		for (i = 0; i < chip->ecc.size; i++)
			nbitflips += hweight8(corrbuf[i] ^ rawbuf[i]);

		offs = (step * 16) + 4;
		rawbuf = micron->ecc.rawbuf + mtd->writesize + offs;
		corrbuf = chip->oob_poi + offs;

		for (i = 0; i < chip->ecc.bytes + 4; i++)
			nbitflips += hweight8(corrbuf[i] ^ rawbuf[i]);

		if (WARN_ON(nbitflips > chip->ecc.strength))
			return -EINVAL;

		max_bitflips = max(nbitflips, max_bitflips);
		mtd->ecc_stats.corrected += nbitflips;
	}

	return max_bitflips;
}

static int micron_nand_on_die_ecc_status_8(struct nand_chip *chip, u8 status)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/*
	 * With 8/512 we have more information but still don't know precisely
	 * how many bit-flips were seen.
	 */
	switch (status & NAND_ECC_STATUS_MASK) {
	case NAND_ECC_STATUS_UNCORRECTABLE:
		mtd->ecc_stats.failed++;
		return 0;
	case NAND_ECC_STATUS_1_3_CORRECTED:
		mtd->ecc_stats.corrected += 3;
		return 3;
	case NAND_ECC_STATUS_4_6_CORRECTED:
		mtd->ecc_stats.corrected += 6;
		/* rewrite recommended */
		return 6;
	case NAND_ECC_STATUS_7_8_CORRECTED:
		mtd->ecc_stats.corrected += 8;
		/* rewrite recommended */
		return 8;
	default:
		return 0;
	}
}

static int
micron_nand_read_page_on_die_ecc(struct nand_chip *chip, uint8_t *buf,
				 int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	bool use_datain = false;
	u8 status;
	int ret, max_bitflips = 0;

	ret = micron_nand_on_die_ecc_setup(chip, true);
	if (ret)
		return ret;

	ret = nand_read_page_op(chip, page, 0, NULL, 0);
	if (ret)
		goto out;

	ret = nand_status_op(chip, &status);
	if (ret)
		goto out;

	/*
	 * We first check which operation is supported by the controller before
	 * running it. This trick makes it possible to support all controllers,
	 * even the most constraints, without almost any performance hit.
	 *
	 * TODO: could be enhanced to avoid repeating the same check over and
	 * over in the fast path.
	 */
	if (!nand_has_exec_op(chip) ||
	    !nand_read_data_op(chip, buf, mtd->writesize, false, true))
		use_datain = true;

	if (use_datain) {
		ret = nand_exit_status_op(chip);
		if (ret)
			goto out;

		ret = nand_read_data_op(chip, buf, mtd->writesize, false,
					false);
		if (!ret && oob_required)
			ret = nand_read_data_op(chip, chip->oob_poi,
						mtd->oobsize, false, false);
	} else {
		ret = nand_change_read_column_op(chip, 0, buf, mtd->writesize,
						 false);
		if (!ret && oob_required)
			ret = nand_change_read_column_op(chip, mtd->writesize,
							 chip->oob_poi,
							 mtd->oobsize, false);
	}

	if (chip->ecc.strength == 4)
		max_bitflips = micron_nand_on_die_ecc_status_4(chip, status,
							       buf, page,
							       oob_required);
	else
		max_bitflips = micron_nand_on_die_ecc_status_8(chip, status);

out:
	micron_nand_on_die_ecc_setup(chip, false);

	return ret ? ret : max_bitflips;
}

static int
micron_nand_write_page_on_die_ecc(struct nand_chip *chip, const uint8_t *buf,
				  int oob_required, int page)
{
	int ret;

	ret = micron_nand_on_die_ecc_setup(chip, true);
	if (ret)
		return ret;

	ret = nand_write_page_raw(chip, buf, oob_required, page);
	micron_nand_on_die_ecc_setup(chip, false);

	return ret;
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

#define MICRON_ID_INTERNAL_ECC_MASK	GENMASK(1, 0)
#define MICRON_ID_ECC_ENABLED		BIT(7)

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
	u8 id[5];
	int ret;

	if (!chip->parameters.onfi)
		return MICRON_ON_DIE_UNSUPPORTED;

	if (nanddev_bits_per_cell(&chip->base) != 1)
		return MICRON_ON_DIE_UNSUPPORTED;

	/*
	 * We only support on-die ECC of 4/512 or 8/512
	 */
	if  (chip->base.eccreq.strength != 4 && chip->base.eccreq.strength != 8)
		return MICRON_ON_DIE_UNSUPPORTED;

	/* 0x2 means on-die ECC is available. */
	if (chip->id.len != 5 ||
	    (chip->id.data[4] & MICRON_ID_INTERNAL_ECC_MASK) != 0x2)
		return MICRON_ON_DIE_UNSUPPORTED;

	/*
	 * It seems that there are devices which do not support ECC officially.
	 * At least the MT29F2G08ABAGA / MT29F2G08ABBGA devices supports
	 * enabling the ECC feature but don't reflect that to the READ_ID table.
	 * So we have to guarantee that we disable the ECC feature directly
	 * after we did the READ_ID table command. Later we can evaluate the
	 * ECC_ENABLE support.
	 */
	ret = micron_nand_on_die_ecc_setup(chip, true);
	if (ret)
		return MICRON_ON_DIE_UNSUPPORTED;

	ret = nand_readid_op(chip, 0, id, sizeof(id));
	if (ret)
		return MICRON_ON_DIE_UNSUPPORTED;

	ret = micron_nand_on_die_ecc_setup(chip, false);
	if (ret)
		return MICRON_ON_DIE_UNSUPPORTED;

	if (!(id[4] & MICRON_ID_ECC_ENABLED))
		return MICRON_ON_DIE_UNSUPPORTED;

	ret = nand_readid_op(chip, 0, id, sizeof(id));
	if (ret)
		return MICRON_ON_DIE_UNSUPPORTED;

	if (id[4] & MICRON_ID_ECC_ENABLED)
		return MICRON_ON_DIE_MANDATORY;

	/*
	 * We only support on-die ECC of 4/512 or 8/512
	 */
	if  (chip->base.eccreq.strength != 4 && chip->base.eccreq.strength != 8)
		return MICRON_ON_DIE_UNSUPPORTED;

	return MICRON_ON_DIE_SUPPORTED;
}

static int micron_nand_init(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct micron_nand *micron;
	int ondie;
	int ret;

	micron = kzalloc(sizeof(*micron), GFP_KERNEL);
	if (!micron)
		return -ENOMEM;

	nand_set_manufacturer_data(chip, micron);

	ret = micron_nand_onfi_init(chip);
	if (ret)
		goto err_free_manuf_data;

	chip->options |= NAND_BBM_FIRSTPAGE;

	if (mtd->writesize == 2048)
		chip->options |= NAND_BBM_SECONDPAGE;

	ondie = micron_supports_on_die_ecc(chip);

	if (ondie == MICRON_ON_DIE_MANDATORY &&
	    chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_ON_DIE) {
		pr_err("On-die ECC forcefully enabled, not supported\n");
		ret = -EINVAL;
		goto err_free_manuf_data;
	}

	if (chip->ecc.engine_type == NAND_ECC_ENGINE_TYPE_ON_DIE) {
		if (ondie == MICRON_ON_DIE_UNSUPPORTED) {
			pr_err("On-die ECC selected but not supported\n");
			ret = -EINVAL;
			goto err_free_manuf_data;
		}

		if (ondie == MICRON_ON_DIE_MANDATORY) {
			micron->ecc.forced = true;
			micron->ecc.enabled = true;
		}

		/*
		 * In case of 4bit on-die ECC, we need a buffer to store a
		 * page dumped in raw mode so that we can compare its content
		 * to the same page after ECC correction happened and extract
		 * the real number of bitflips from this comparison.
		 * That's not needed for 8-bit ECC, because the status expose
		 * a better approximation of the number of bitflips in a page.
		 */
		if (chip->base.eccreq.strength == 4) {
			micron->ecc.rawbuf = kmalloc(mtd->writesize +
						     mtd->oobsize,
						     GFP_KERNEL);
			if (!micron->ecc.rawbuf) {
				ret = -ENOMEM;
				goto err_free_manuf_data;
			}
		}

		if (chip->base.eccreq.strength == 4)
			mtd_set_ooblayout(mtd,
					  &micron_nand_on_die_4_ooblayout_ops);
		else
			mtd_set_ooblayout(mtd,
					  &micron_nand_on_die_8_ooblayout_ops);

		chip->ecc.bytes = chip->base.eccreq.strength * 2;
		chip->ecc.size = 512;
		chip->ecc.strength = chip->base.eccreq.strength;
		chip->ecc.algo = NAND_ECC_ALGO_BCH;
		chip->ecc.read_page = micron_nand_read_page_on_die_ecc;
		chip->ecc.write_page = micron_nand_write_page_on_die_ecc;

		if (ondie == MICRON_ON_DIE_MANDATORY) {
			chip->ecc.read_page_raw = nand_read_page_raw_notsupp;
			chip->ecc.write_page_raw = nand_write_page_raw_notsupp;
		} else {
			if (!chip->ecc.read_page_raw)
				chip->ecc.read_page_raw = nand_read_page_raw;
			if (!chip->ecc.write_page_raw)
				chip->ecc.write_page_raw = nand_write_page_raw;
		}
	}

	return 0;

err_free_manuf_data:
	kfree(micron->ecc.rawbuf);
	kfree(micron);

	return ret;
}

static void micron_nand_cleanup(struct nand_chip *chip)
{
	struct micron_nand *micron = nand_get_manufacturer_data(chip);

	kfree(micron->ecc.rawbuf);
	kfree(micron);
}

static void micron_fixup_onfi_param_page(struct nand_chip *chip,
					 struct nand_onfi_params *p)
{
	/*
	 * MT29F1G08ABAFAWP-ITE:F and possibly others report 00 00 for the
	 * revision number field of the ONFI parameter page. Assume ONFI
	 * version 1.0 if the revision number is 00 00.
	 */
	if (le16_to_cpu(p->revision) == 0)
		p->revision = cpu_to_le16(ONFI_VERSION_1_0);
}

const struct nand_manufacturer_ops micron_nand_manuf_ops = {
	.init = micron_nand_init,
	.cleanup = micron_nand_cleanup,
	.fixup_onfi_param_page = micron_fixup_onfi_param_page,
};
