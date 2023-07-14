// SPDX-License-Identifier: GPL-2.0-only
/*
 * Voltage regulators coupler for MediaTek SoCs
 *
 * Copyright (C) 2022 Collabora, Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/suspend.h>

#define to_mediatek_coupler(x)	container_of(x, struct mediatek_regulator_coupler, coupler)

struct mediatek_regulator_coupler {
	struct regulator_coupler coupler;
	struct regulator_dev *vsram_rdev;
};

/*
 * We currently support only couples of not more than two vregs and
 * modify the vsram voltage only when changing voltage of vgpu.
 *
 * This function is limited to the GPU<->SRAM voltages relationships.
 */
static int mediatek_regulator_balance_voltage(struct regulator_coupler *coupler,
					      struct regulator_dev *rdev,
					      suspend_state_t state)
{
	struct mediatek_regulator_coupler *mrc = to_mediatek_coupler(coupler);
	int max_spread = rdev->constraints->max_spread[0];
	int vsram_min_uV = mrc->vsram_rdev->constraints->min_uV;
	int vsram_max_uV = mrc->vsram_rdev->constraints->max_uV;
	int vsram_target_min_uV, vsram_target_max_uV;
	int min_uV = 0;
	int max_uV = INT_MAX;
	int ret;

	/*
	 * If the target device is on, setting the SRAM voltage directly
	 * is not supported as it scales through its coupled supply voltage.
	 *
	 * An exception is made in case the use_count is zero: this means
	 * that this is the first time we power up the SRAM regulator, which
	 * implies that the target device has yet to perform initialization
	 * and setting a voltage at that time is harmless.
	 */
	if (rdev == mrc->vsram_rdev) {
		if (rdev->use_count == 0)
			return regulator_do_balance_voltage(rdev, state, true);

		return -EPERM;
	}

	ret = regulator_check_consumers(rdev, &min_uV, &max_uV, state);
	if (ret < 0)
		return ret;

	if (min_uV == 0) {
		ret = regulator_get_voltage_rdev(rdev);
		if (ret < 0)
			return ret;
		min_uV = ret;
	}

	ret = regulator_check_voltage(rdev, &min_uV, &max_uV);
	if (ret < 0)
		return ret;

	/*
	 * If we're asked to set a voltage less than VSRAM min_uV, set
	 * the minimum allowed voltage on VSRAM, as in this case it is
	 * safe to ignore the max_spread parameter.
	 */
	vsram_target_min_uV = max(vsram_min_uV, min_uV + max_spread);
	vsram_target_max_uV = min(vsram_max_uV, vsram_target_min_uV + max_spread);

	/* Make sure we're not out of range */
	vsram_target_min_uV = min(vsram_target_min_uV, vsram_max_uV);

	pr_debug("Setting voltage %d-%duV on %s (minuV %d)\n",
		 vsram_target_min_uV, vsram_target_max_uV,
		 rdev_get_name(mrc->vsram_rdev), min_uV);

	ret = regulator_set_voltage_rdev(mrc->vsram_rdev, vsram_target_min_uV,
					 vsram_target_max_uV, state);
	if (ret)
		return ret;

	/* The sram voltage is now balanced: update the target vreg voltage */
	return regulator_do_balance_voltage(rdev, state, true);
}

static int mediatek_regulator_attach(struct regulator_coupler *coupler,
				     struct regulator_dev *rdev)
{
	struct mediatek_regulator_coupler *mrc = to_mediatek_coupler(coupler);
	const char *rdev_name = rdev_get_name(rdev);

	/*
	 * If we're getting a coupling of more than two regulators here and
	 * this means that this is surely not a GPU<->SRAM couple: in that
	 * case, we may want to use another coupler implementation, if any,
	 * or the generic one: the regulator core will keep walking through
	 * the list of couplers when any .attach_regulator() cb returns 1.
	 */
	if (rdev->coupling_desc.n_coupled > 2)
		return 1;

	if (strstr(rdev_name, "sram")) {
		if (mrc->vsram_rdev)
			return -EINVAL;
		mrc->vsram_rdev = rdev;
	} else if (!strstr(rdev_name, "vgpu") && !strstr(rdev_name, "Vgpu")) {
		return 1;
	}

	return 0;
}

static int mediatek_regulator_detach(struct regulator_coupler *coupler,
				     struct regulator_dev *rdev)
{
	struct mediatek_regulator_coupler *mrc = to_mediatek_coupler(coupler);

	if (rdev == mrc->vsram_rdev)
		mrc->vsram_rdev = NULL;

	return 0;
}

static struct mediatek_regulator_coupler mediatek_coupler = {
	.coupler = {
		.attach_regulator = mediatek_regulator_attach,
		.detach_regulator = mediatek_regulator_detach,
		.balance_voltage = mediatek_regulator_balance_voltage,
	},
};

static int mediatek_regulator_coupler_init(void)
{
	if (!of_machine_is_compatible("mediatek,mt8183") &&
	    !of_machine_is_compatible("mediatek,mt8186") &&
	    !of_machine_is_compatible("mediatek,mt8192"))
		return 0;

	return regulator_coupler_register(&mediatek_coupler.coupler);
}
arch_initcall(mediatek_regulator_coupler_init);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek Regulator Coupler driver");
MODULE_LICENSE("GPL");
