// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Free Electrons
 * Copyright (C) 2017 NextThing Co
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 */

#include "internals.h"

#define MACRONIX_READ_RETRY_BIT BIT(0)
#define MACRONIX_NUM_READ_RETRY_MODES 6

struct nand_onfi_vendor_macronix {
	u8 reserved;
	u8 reliability_func;
} __packed;

static int macronix_nand_setup_read_retry(struct nand_chip *chip, int mode)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN];

	if (!chip->parameters.supports_set_get_features ||
	    !test_bit(ONFI_FEATURE_ADDR_READ_RETRY,
		      chip->parameters.set_feature_list))
		return -ENOTSUPP;

	feature[0] = mode;
	return nand_set_features(chip, ONFI_FEATURE_ADDR_READ_RETRY, feature);
}

static void macronix_nand_onfi_init(struct nand_chip *chip)
{
	struct nand_parameters *p = &chip->parameters;
	struct nand_onfi_vendor_macronix *mxic;

	if (!p->onfi)
		return;

	mxic = (struct nand_onfi_vendor_macronix *)p->onfi->vendor;
	if ((mxic->reliability_func & MACRONIX_READ_RETRY_BIT) == 0)
		return;

	chip->read_retries = MACRONIX_NUM_READ_RETRY_MODES;
	chip->setup_read_retry = macronix_nand_setup_read_retry;

	if (p->supports_set_get_features) {
		bitmap_set(p->set_feature_list,
			   ONFI_FEATURE_ADDR_READ_RETRY, 1);
		bitmap_set(p->get_feature_list,
			   ONFI_FEATURE_ADDR_READ_RETRY, 1);
	}
}

/*
 * Macronix AC series does not support using SET/GET_FEATURES to change
 * the timings unlike what is declared in the parameter page. Unflag
 * this feature to avoid unnecessary downturns.
 */
static void macronix_nand_fix_broken_get_timings(struct nand_chip *chip)
{
	unsigned int i;
	static const char * const broken_get_timings[] = {
		"MX30LF1G18AC",
		"MX30LF1G28AC",
		"MX30LF2G18AC",
		"MX30LF2G28AC",
		"MX30LF4G18AC",
		"MX30LF4G28AC",
		"MX60LF8G18AC",
		"MX30UF1G18AC",
		"MX30UF1G16AC",
		"MX30UF2G18AC",
		"MX30UF2G16AC",
		"MX30UF4G18AC",
		"MX30UF4G16AC",
		"MX30UF4G28AC",
	};

	if (!chip->parameters.supports_set_get_features)
		return;

	for (i = 0; i < ARRAY_SIZE(broken_get_timings); i++) {
		if (!strcmp(broken_get_timings[i], chip->parameters.model))
			break;
	}

	if (i == ARRAY_SIZE(broken_get_timings))
		return;

	bitmap_clear(chip->parameters.get_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
	bitmap_clear(chip->parameters.set_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
}

static int macronix_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->options |= NAND_BBM_FIRSTPAGE | NAND_BBM_SECONDPAGE;

	macronix_nand_fix_broken_get_timings(chip);
	macronix_nand_onfi_init(chip);

	return 0;
}

const struct nand_manufacturer_ops macronix_nand_manuf_ops = {
	.init = macronix_nand_init,
};
