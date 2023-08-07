// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Free Electrons
 * Copyright (C) 2017 NextThing Co
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 */

#include <linux/slab.h>
#include "linux/delay.h"
#include "internals.h"

#define MACRONIX_READ_RETRY_BIT BIT(0)
#define MACRONIX_NUM_READ_RETRY_MODES 6

#define ONFI_FEATURE_ADDR_MXIC_PROTECTION 0xA0
#define MXIC_BLOCK_PROTECTION_ALL_LOCK 0x38
#define MXIC_BLOCK_PROTECTION_ALL_UNLOCK 0x0

#define ONFI_FEATURE_ADDR_MXIC_RANDOMIZER 0xB0
#define MACRONIX_RANDOMIZER_BIT BIT(1)
#define MACRONIX_RANDOMIZER_ENPGM BIT(0)
#define MACRONIX_RANDOMIZER_RANDEN BIT(1)
#define MACRONIX_RANDOMIZER_RANDOPT BIT(2)
#define MACRONIX_RANDOMIZER_MODE_ENTER	\
	(MACRONIX_RANDOMIZER_ENPGM |	\
	 MACRONIX_RANDOMIZER_RANDEN |	\
	 MACRONIX_RANDOMIZER_RANDOPT)
#define MACRONIX_RANDOMIZER_MODE_EXIT	\
	(MACRONIX_RANDOMIZER_RANDEN |	\
	 MACRONIX_RANDOMIZER_RANDOPT)

#define MXIC_CMD_POWER_DOWN 0xB9

#define ONFI_FEATURE_ADDR_30LFXG18AC_OTP	0x90
#define MACRONIX_30LFXG18AC_OTP_START_PAGE	2
#define MACRONIX_30LFXG18AC_OTP_PAGES		30
#define MACRONIX_30LFXG18AC_OTP_PAGE_SIZE	2112
#define MACRONIX_30LFXG18AC_OTP_SIZE_BYTES	\
	(MACRONIX_30LFXG18AC_OTP_PAGES *	\
	 MACRONIX_30LFXG18AC_OTP_PAGE_SIZE)

#define MACRONIX_30LFXG18AC_OTP_EN		BIT(0)

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

static int macronix_nand_randomizer_check_enable(struct nand_chip *chip)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN];
	int ret;

	ret = nand_get_features(chip, ONFI_FEATURE_ADDR_MXIC_RANDOMIZER,
				feature);
	if (ret < 0)
		return ret;

	if (feature[0])
		return feature[0];

	feature[0] = MACRONIX_RANDOMIZER_MODE_ENTER;
	ret = nand_set_features(chip, ONFI_FEATURE_ADDR_MXIC_RANDOMIZER,
				feature);
	if (ret < 0)
		return ret;

	/* RANDEN and RANDOPT OTP bits are programmed */
	feature[0] = 0x0;
	ret = nand_prog_page_op(chip, 0, 0, feature, 1);
	if (ret < 0)
		return ret;

	ret = nand_get_features(chip, ONFI_FEATURE_ADDR_MXIC_RANDOMIZER,
				feature);
	if (ret < 0)
		return ret;

	feature[0] &= MACRONIX_RANDOMIZER_MODE_EXIT;
	ret = nand_set_features(chip, ONFI_FEATURE_ADDR_MXIC_RANDOMIZER,
				feature);
	if (ret < 0)
		return ret;

	return 0;
}

static void macronix_nand_onfi_init(struct nand_chip *chip)
{
	struct nand_parameters *p = &chip->parameters;
	struct nand_onfi_vendor_macronix *mxic;
	struct device_node *dn = nand_get_flash_node(chip);
	int rand_otp;
	int ret;

	if (!p->onfi)
		return;

	rand_otp = of_property_read_bool(dn, "mxic,enable-randomizer-otp");

	mxic = (struct nand_onfi_vendor_macronix *)p->onfi->vendor;
	/* Subpage write is prohibited in randomizer operatoin */
	if (rand_otp && chip->options & NAND_NO_SUBPAGE_WRITE &&
	    mxic->reliability_func & MACRONIX_RANDOMIZER_BIT) {
		if (p->supports_set_get_features) {
			bitmap_set(p->set_feature_list,
				   ONFI_FEATURE_ADDR_MXIC_RANDOMIZER, 1);
			bitmap_set(p->get_feature_list,
				   ONFI_FEATURE_ADDR_MXIC_RANDOMIZER, 1);
			ret = macronix_nand_randomizer_check_enable(chip);
			if (ret < 0) {
				bitmap_clear(p->set_feature_list,
					     ONFI_FEATURE_ADDR_MXIC_RANDOMIZER,
					     1);
				bitmap_clear(p->get_feature_list,
					     ONFI_FEATURE_ADDR_MXIC_RANDOMIZER,
					     1);
				pr_info("Macronix NAND randomizer failed\n");
			} else {
				pr_info("Macronix NAND randomizer enabled\n");
			}
		}
	}

	if ((mxic->reliability_func & MACRONIX_READ_RETRY_BIT) == 0)
		return;

	chip->read_retries = MACRONIX_NUM_READ_RETRY_MODES;
	chip->ops.setup_read_retry = macronix_nand_setup_read_retry;

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
	int i;
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

	i = match_string(broken_get_timings, ARRAY_SIZE(broken_get_timings),
			 chip->parameters.model);
	if (i < 0)
		return;

	bitmap_clear(chip->parameters.get_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
	bitmap_clear(chip->parameters.set_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
}

/*
 * Macronix NAND supports Block Protection by Protectoin(PT) pin;
 * active high at power-on which protects the entire chip even the #WP is
 * disabled. Lock/unlock protection area can be partition according to
 * protection bits, i.e. upper 1/2 locked, upper 1/4 locked and so on.
 */
static int mxic_nand_lock(struct nand_chip *chip, loff_t ofs, uint64_t len)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN];
	int ret;

	feature[0] = MXIC_BLOCK_PROTECTION_ALL_LOCK;
	nand_select_target(chip, 0);
	ret = nand_set_features(chip, ONFI_FEATURE_ADDR_MXIC_PROTECTION,
				feature);
	nand_deselect_target(chip);
	if (ret)
		pr_err("%s all blocks failed\n", __func__);

	return ret;
}

static int mxic_nand_unlock(struct nand_chip *chip, loff_t ofs, uint64_t len)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN];
	int ret;

	feature[0] = MXIC_BLOCK_PROTECTION_ALL_UNLOCK;
	nand_select_target(chip, 0);
	ret = nand_set_features(chip, ONFI_FEATURE_ADDR_MXIC_PROTECTION,
				feature);
	nand_deselect_target(chip);
	if (ret)
		pr_err("%s all blocks failed\n", __func__);

	return ret;
}

static void macronix_nand_block_protection_support(struct nand_chip *chip)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN];
	int ret;

	bitmap_set(chip->parameters.get_feature_list,
		   ONFI_FEATURE_ADDR_MXIC_PROTECTION, 1);

	feature[0] = MXIC_BLOCK_PROTECTION_ALL_UNLOCK;
	nand_select_target(chip, 0);
	ret = nand_get_features(chip, ONFI_FEATURE_ADDR_MXIC_PROTECTION,
				feature);
	nand_deselect_target(chip);
	if (ret || feature[0] != MXIC_BLOCK_PROTECTION_ALL_LOCK) {
		if (ret)
			pr_err("Block protection check failed\n");

		bitmap_clear(chip->parameters.get_feature_list,
			     ONFI_FEATURE_ADDR_MXIC_PROTECTION, 1);
		return;
	}

	bitmap_set(chip->parameters.set_feature_list,
		   ONFI_FEATURE_ADDR_MXIC_PROTECTION, 1);

	chip->ops.lock_area = mxic_nand_lock;
	chip->ops.unlock_area = mxic_nand_unlock;
}

static int nand_power_down_op(struct nand_chip *chip)
{
	int ret;

	if (nand_has_exec_op(chip)) {
		struct nand_op_instr instrs[] = {
			NAND_OP_CMD(MXIC_CMD_POWER_DOWN, 0),
		};

		struct nand_operation op = NAND_OPERATION(chip->cur_cs, instrs);

		ret = nand_exec_op(chip, &op);
		if (ret)
			return ret;

	} else {
		chip->legacy.cmdfunc(chip, MXIC_CMD_POWER_DOWN, -1, -1);
	}

	return 0;
}

static int mxic_nand_suspend(struct nand_chip *chip)
{
	int ret;

	nand_select_target(chip, 0);
	ret = nand_power_down_op(chip);
	if (ret < 0)
		pr_err("Suspending MXIC NAND chip failed (%d)\n", ret);
	nand_deselect_target(chip);

	return ret;
}

static void mxic_nand_resume(struct nand_chip *chip)
{
	/*
	 * Toggle #CS pin to resume NAND device and don't care
	 * of the others CLE, #WE, #RE pins status.
	 * A NAND controller ensure it is able to assert/de-assert #CS
	 * by sending any byte over the NAND bus.
	 * i.e.,
	 * NAND power down command or reset command w/o R/B# status checking.
	 */
	nand_select_target(chip, 0);
	nand_power_down_op(chip);
	/* The minimum of a recovery time tRDP is 35 us */
	usleep_range(35, 100);
	nand_deselect_target(chip);
}

static void macronix_nand_deep_power_down_support(struct nand_chip *chip)
{
	int i;
	static const char * const deep_power_down_dev[] = {
		"MX30UF1G28AD",
		"MX30UF2G28AD",
		"MX30UF4G28AD",
	};

	i = match_string(deep_power_down_dev, ARRAY_SIZE(deep_power_down_dev),
			 chip->parameters.model);
	if (i < 0)
		return;

	chip->ops.suspend = mxic_nand_suspend;
	chip->ops.resume = mxic_nand_resume;
}

static int macronix_30lfxg18ac_get_otp_info(struct mtd_info *mtd, size_t len,
					    size_t *retlen,
					    struct otp_info *buf)
{
	if (len < sizeof(*buf))
		return -EINVAL;

	/* Always report that OTP is unlocked. Reason is that this
	 * type of flash chip doesn't provide way to check that OTP
	 * is locked or not: subfeature parameter is implemented as
	 * volatile register. Technically OTP region could be locked
	 * and become readonly, but as there is no way to check it,
	 * don't allow to lock it ('_lock_user_prot_reg' callback
	 * always returns -EOPNOTSUPP) and thus we report that OTP
	 * is unlocked.
	 */
	buf->locked = 0;
	buf->start = 0;
	buf->length = MACRONIX_30LFXG18AC_OTP_SIZE_BYTES;

	*retlen = sizeof(*buf);

	return 0;
}

static int macronix_30lfxg18ac_otp_enable(struct nand_chip *nand)
{
	u8 feature_buf[ONFI_SUBFEATURE_PARAM_LEN] = { 0 };

	feature_buf[0] = MACRONIX_30LFXG18AC_OTP_EN;
	return nand_set_features(nand, ONFI_FEATURE_ADDR_30LFXG18AC_OTP,
				 feature_buf);
}

static int macronix_30lfxg18ac_otp_disable(struct nand_chip *nand)
{
	u8 feature_buf[ONFI_SUBFEATURE_PARAM_LEN] = { 0 };

	return nand_set_features(nand, ONFI_FEATURE_ADDR_30LFXG18AC_OTP,
				 feature_buf);
}

static int __macronix_30lfxg18ac_rw_otp(struct mtd_info *mtd,
					loff_t offs_in_flash,
					size_t len, size_t *retlen,
					u_char *buf, bool write)
{
	struct nand_chip *nand;
	size_t bytes_handled;
	off_t offs_in_page;
	u64 page;
	int ret;

	nand = mtd_to_nand(mtd);
	nand_select_target(nand, 0);

	ret = macronix_30lfxg18ac_otp_enable(nand);
	if (ret)
		goto out_otp;

	page = offs_in_flash;
	/* 'page' will be result of division. */
	offs_in_page = do_div(page, MACRONIX_30LFXG18AC_OTP_PAGE_SIZE);
	bytes_handled = 0;

	while (bytes_handled < len &&
	       page < MACRONIX_30LFXG18AC_OTP_PAGES) {
		size_t bytes_to_handle;
		u64 phys_page = page + MACRONIX_30LFXG18AC_OTP_START_PAGE;

		bytes_to_handle = min_t(size_t, len - bytes_handled,
					MACRONIX_30LFXG18AC_OTP_PAGE_SIZE -
					offs_in_page);

		if (write)
			ret = nand_prog_page_op(nand, phys_page, offs_in_page,
						&buf[bytes_handled], bytes_to_handle);
		else
			ret = nand_read_page_op(nand, phys_page, offs_in_page,
						&buf[bytes_handled], bytes_to_handle);
		if (ret)
			goto out_otp;

		bytes_handled += bytes_to_handle;
		offs_in_page = 0;
		page++;
	}

	*retlen = bytes_handled;

out_otp:
	if (ret)
		dev_err(&mtd->dev, "failed to perform OTP IO: %i\n", ret);

	ret = macronix_30lfxg18ac_otp_disable(nand);
	if (ret)
		dev_err(&mtd->dev, "failed to leave OTP mode after %s\n",
			write ? "write" : "read");

	nand_deselect_target(nand);

	return ret;
}

static int macronix_30lfxg18ac_write_otp(struct mtd_info *mtd, loff_t to,
					 size_t len, size_t *rlen,
					 const u_char *buf)
{
	return __macronix_30lfxg18ac_rw_otp(mtd, to, len, rlen, (u_char *)buf,
					    true);
}

static int macronix_30lfxg18ac_read_otp(struct mtd_info *mtd, loff_t from,
					size_t len, size_t *rlen,
					u_char *buf)
{
	return __macronix_30lfxg18ac_rw_otp(mtd, from, len, rlen, buf, false);
}

static int macronix_30lfxg18ac_lock_otp(struct mtd_info *mtd, loff_t from,
					size_t len)
{
	/* See comment in 'macronix_30lfxg18ac_get_otp_info()'. */
	return -EOPNOTSUPP;
}

static void macronix_nand_setup_otp(struct nand_chip *chip)
{
	static const char * const supported_otp_models[] = {
		"MX30LF1G18AC",
		"MX30LF2G18AC",
		"MX30LF4G18AC",
	};
	struct mtd_info *mtd;

	if (match_string(supported_otp_models,
			 ARRAY_SIZE(supported_otp_models),
			 chip->parameters.model) < 0)
		return;

	if (!chip->parameters.supports_set_get_features)
		return;

	bitmap_set(chip->parameters.get_feature_list,
		   ONFI_FEATURE_ADDR_30LFXG18AC_OTP, 1);
	bitmap_set(chip->parameters.set_feature_list,
		   ONFI_FEATURE_ADDR_30LFXG18AC_OTP, 1);

	mtd = nand_to_mtd(chip);
	mtd->_get_user_prot_info = macronix_30lfxg18ac_get_otp_info;
	mtd->_read_user_prot_reg = macronix_30lfxg18ac_read_otp;
	mtd->_write_user_prot_reg = macronix_30lfxg18ac_write_otp;
	mtd->_lock_user_prot_reg = macronix_30lfxg18ac_lock_otp;
}

static int macronix_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->options |= NAND_BBM_FIRSTPAGE | NAND_BBM_SECONDPAGE;

	macronix_nand_fix_broken_get_timings(chip);
	macronix_nand_onfi_init(chip);
	macronix_nand_block_protection_support(chip);
	macronix_nand_deep_power_down_support(chip);
	macronix_nand_setup_otp(chip);

	return 0;
}

const struct nand_manufacturer_ops macronix_nand_manuf_ops = {
	.init = macronix_nand_init,
};
