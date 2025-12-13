// SPDX-License-Identifier: GPL-2.0-only
/*
 * Battery driver for the coulomb-counter of the Intel Dollar Cove TI PMIC
 *
 * Note the Intel Dollar Cove TI PMIC coulomb-counter is not a full-featured
 * autonomous fuel-gauge. It is intended to work together with an always on
 * micro-controller monitoring it.
 *
 * Since Linux does not monitor coulomb-counter changes while the device
 * is off or suspended, voltage based capacity estimation from
 * the adc-battery-helper code is used.
 *
 * Copyright (C) 2024 Hans de Goede <hansg@kernel.org>
 *
 * Register definitions and calibration code was taken from
 * kernel/drivers/platform/x86/dc_ti_cc.c from the Acer A1-840 Android kernel
 * which has the following copyright header:
 *
 * Copyright (C) 2014 Intel Corporation
 * Author: Ramakrishna Pallala <ramakrishna.pallala@intel.com>
 *
 * dc_ti_cc.c is part of the Acer A1-840 Android kernel source-code archive
 * named: "App. Guide_Acer_20151221_A_A.zip"
 * which is distributed by Acer from the Acer A1-840 support page:
 * https://www.acer.com/us-en/support/product-support/A1-840/downloads
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/timekeeping.h>

#include "adc-battery-helper.h"

#define DC_TI_PMIC_VERSION_REG		0x00
#define PMIC_VERSION_A0			0xC0
#define PMIC_VERSION_A1			0xC1

#define DC_TI_CC_CNTL_REG		0x60
#define CC_CNTL_CC_CTR_EN		BIT(0)
#define CC_CNTL_CC_CLR_EN		BIT(1)
#define CC_CNTL_CC_CAL_EN		BIT(2)
#define CC_CNTL_CC_OFFSET_EN		BIT(3)
#define CC_CNTL_SMPL_INTVL		GENMASK(5, 4)
#define CC_CNTL_SMPL_INTVL_15MS		FIELD_PREP(CC_CNTL_SMPL_INTVL, 0)
#define CC_CNTL_SMPL_INTVL_62MS		FIELD_PREP(CC_CNTL_SMPL_INTVL, 1)
#define CC_CNTL_SMPL_INTVL_125MS	FIELD_PREP(CC_CNTL_SMPL_INTVL, 2)
#define CC_CNTL_SMPL_INTVL_250MS	FIELD_PREP(CC_CNTL_SMPL_INTVL, 3)

#define DC_TI_SMPL_CTR0_REG		0x69
#define DC_TI_SMPL_CTR1_REG		0x68
#define DC_TI_SMPL_CTR2_REG		0x67

#define DC_TI_CC_OFFSET_HI_REG		0x61
#define CC_OFFSET_HI_MASK		0x3F
#define DC_TI_CC_OFFSET_LO_REG		0x62

#define DC_TI_SW_OFFSET_REG		0x6C

#define DC_TI_CC_ACC3_REG		0x63
#define DC_TI_CC_ACC2_REG		0x64
#define DC_TI_CC_ACC1_REG		0x65
#define DC_TI_CC_ACC0_REG		0x66

#define DC_TI_CC_INTG1_REG		0x6A
#define DC_TI_CC_INTG1_MASK		0x3F
#define DC_TI_CC_INTG0_REG		0x6B

#define DC_TI_EEPROM_ACCESS_CONTROL	0x88
#define EEPROM_UNLOCK			0xDA
#define EEPROM_LOCK			0x00

#define DC_TI_EEPROM_CC_GAIN_REG	0xF4
#define CC_TRIM_REVISION		GENMASK(3, 0)
#define CC_GAIN_CORRECTION		GENMASK(7, 4)

#define PMIC_VERSION_A0_TRIM_REV	3
#define PMIC_VERSION_A1_MIN_TRIM_REV	1

#define DC_TI_EEPROM_CC_OFFSET_REG	0xFD

#define DC_TI_EEPROM_CTRL		0xFE
#define EEPROM_BANK0_SEL		0x01
#define EEPROM_BANK1_SEL		0x02

#define SMPL_INTVL_US			15000
#define SMPL_INTVL_MS			(SMPL_INTVL_US / USEC_PER_MSEC)
#define CALIBRATION_TIME_US		(10 * SMPL_INTVL_US)
#define SLEEP_SLACK_US			2500

/* CC gain correction is in 0.0025 increments */
#define CC_GAIN_STEP			25
#define CC_GAIN_DIV			10000

/* CC offset is in 0.5 units per 250ms (default sample interval) */
#define CC_OFFSET_DIV			2
#define CC_OFFSET_SMPL_INTVL_MS		250

/* CC accumulator scale is 366.2 ųCoulumb / unit */
#define CC_ACC_TO_UA(acc, smpl_ctr)	\
	((acc) * (3662 * MSEC_PER_SEC / 10) / ((smpl_ctr) * SMPL_INTVL_MS))

#define DEV_NAME			"chtdc_ti_battery"

struct dc_ti_battery_chip {
	/* Must be the first member see adc-battery-helper documentation */
	struct adc_battery_helper helper;
	struct device *dev;
	struct regmap *regmap;
	struct iio_channel *vbat_channel;
	struct power_supply *psy;
	int cc_gain;
	int cc_offset;
};

static int dc_ti_battery_get_voltage_and_current_now(struct power_supply *psy, int *volt, int *curr)
{
	struct dc_ti_battery_chip *chip = power_supply_get_drvdata(psy);
	ktime_t ktime;
	s64 sleep_usec;
	unsigned int reg_val;
	s32 acc, smpl_ctr;
	int ret;

	/*
	 * Enable coulomb-counter before reading Vbat from ADC, so that the CC
	 * samples are from the same time period as the Vbat reading.
	 */
	ret = regmap_write(chip->regmap, DC_TI_CC_CNTL_REG,
			   CC_CNTL_SMPL_INTVL_15MS | CC_CNTL_CC_OFFSET_EN | CC_CNTL_CC_CTR_EN);
	if (ret)
		goto out_err;

	ktime = ktime_get();

	/* Read Vbat, convert IIO mV to power-supply ųV */
	ret = iio_read_channel_processed_scale(chip->vbat_channel, volt, 1000);
	if (ret < 0)
		goto out_err;

	ktime = ktime_sub(ktime_get(), ktime);

	/* Sleep at least 3 sample-times + slack to get 3+ CC samples */
	sleep_usec = 3 * SMPL_INTVL_US + SLEEP_SLACK_US - ktime_to_us(ktime);
	if (sleep_usec > 0 && sleep_usec < 1000000)
		usleep_range(sleep_usec, sleep_usec + SLEEP_SLACK_US);

	/*
	 * The PMIC latches the coulomb- and sample-counters upon reading the
	 * CC_ACC0 register. Reading multiple registers at once is not supported.
	 *
	 * Step 1: Read CC_ACC0 - CC_ACC3
	 */
	ret = regmap_read(chip->regmap, DC_TI_CC_ACC0_REG, &reg_val);
	if (ret)
		goto out_err;

	acc = reg_val;

	ret = regmap_read(chip->regmap, DC_TI_CC_ACC1_REG, &reg_val);
	if (ret)
		goto out_err;

	acc |= reg_val << 8;

	ret = regmap_read(chip->regmap, DC_TI_CC_ACC2_REG, &reg_val);
	if (ret)
		goto out_err;

	acc |= reg_val << 16;

	ret = regmap_read(chip->regmap, DC_TI_CC_ACC3_REG, &reg_val);
	if (ret)
		goto out_err;

	acc |= reg_val << 24;

	/* Step 2: Read SMPL_CTR0 - SMPL_CTR2 */
	ret = regmap_read(chip->regmap, DC_TI_SMPL_CTR0_REG, &reg_val);
	if (ret)
		goto out_err;

	smpl_ctr = reg_val;

	ret = regmap_read(chip->regmap, DC_TI_SMPL_CTR1_REG, &reg_val);
	if (ret)
		goto out_err;

	smpl_ctr |= reg_val << 8;

	ret = regmap_read(chip->regmap, DC_TI_SMPL_CTR2_REG, &reg_val);
	if (ret)
		goto out_err;

	smpl_ctr |= reg_val << 16;

	/* Disable the coulumb-counter again */
	ret = regmap_write(chip->regmap, DC_TI_CC_CNTL_REG,
			   CC_CNTL_SMPL_INTVL_15MS | CC_CNTL_CC_OFFSET_EN);
	if (ret)
		goto out_err;

	/* Apply calibration */
	acc -= chip->cc_offset * smpl_ctr * SMPL_INTVL_MS /
	       (CC_OFFSET_SMPL_INTVL_MS * CC_OFFSET_DIV);
	acc = acc * (CC_GAIN_DIV - chip->cc_gain * CC_GAIN_STEP) / CC_GAIN_DIV;
	*curr = CC_ACC_TO_UA(acc, smpl_ctr);

	return 0;

out_err:
	dev_err(chip->dev, "IO-error %d communicating with PMIC\n", ret);
	return ret;
}

static const struct power_supply_desc dc_ti_battery_psy_desc = {
	.name		= "intel_dc_ti_battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= adc_battery_helper_get_property,
	.external_power_changed	= adc_battery_helper_external_power_changed,
	.properties	= adc_battery_helper_properties,
	.num_properties	= ADC_HELPER_NUM_PROPERTIES,
};

static int dc_ti_battery_hw_init(struct dc_ti_battery_chip *chip)
{
	u8 pmic_version, cc_trim_rev;
	unsigned int reg_val;
	int ret;

	/* Set sample rate to 15 ms and calibrate the coulomb-counter */
	ret = regmap_write(chip->regmap, DC_TI_CC_CNTL_REG,
			   CC_CNTL_SMPL_INTVL_15MS | CC_CNTL_CC_OFFSET_EN |
			   CC_CNTL_CC_CAL_EN | CC_CNTL_CC_CTR_EN);
	if (ret)
		goto out;

	fsleep(CALIBRATION_TIME_US);

	/* Disable coulomb-counter it is only used while getting the current */
	ret = regmap_write(chip->regmap, DC_TI_CC_CNTL_REG,
			   CC_CNTL_SMPL_INTVL_15MS | CC_CNTL_CC_OFFSET_EN);
	if (ret)
		goto out;

	ret = regmap_read(chip->regmap, DC_TI_PMIC_VERSION_REG, &reg_val);
	if (ret)
		goto out;

	pmic_version = reg_val;

	/*
	 * As per the PMIC vendor (TI), the calibration offset and gain err
	 * values are stored in EEPROM Bank 0 and Bank 1 of the PMIC.
	 * We need to read the stored offset and gain margins and need
	 * to apply the corrections to the raw coulomb counter value.
	 */

	/* Unlock the EEPROM Access */
	ret = regmap_write(chip->regmap, DC_TI_EEPROM_ACCESS_CONTROL, EEPROM_UNLOCK);
	if (ret)
		goto out;

	/* Select Bank 1 to read CC GAIN Err correction */
	ret = regmap_write(chip->regmap, DC_TI_EEPROM_CTRL, EEPROM_BANK1_SEL);
	if (ret)
		goto out;

	ret = regmap_read(chip->regmap, DC_TI_EEPROM_CC_GAIN_REG, &reg_val);
	if (ret)
		goto out;

	cc_trim_rev = FIELD_GET(CC_TRIM_REVISION, reg_val);

	dev_dbg(chip->dev, "pmic-ver 0x%02x trim-rev %d\n", pmic_version, cc_trim_rev);

	if (!(pmic_version == PMIC_VERSION_A0 && cc_trim_rev == PMIC_VERSION_A0_TRIM_REV) &&
	    !(pmic_version == PMIC_VERSION_A1 && cc_trim_rev >= PMIC_VERSION_A1_MIN_TRIM_REV)) {
		dev_dbg(chip->dev, "unsupported trim-revision, using uncalibrated CC values\n");
		goto out_relock;
	}

	chip->cc_gain = 1 - (int)FIELD_GET(CC_GAIN_CORRECTION, reg_val);

	/* Select Bank 0 to read CC OFFSET Correction */
	ret = regmap_write(chip->regmap, DC_TI_EEPROM_CTRL, EEPROM_BANK0_SEL);
	if (ret)
		goto out_relock;

	ret = regmap_read(chip->regmap, DC_TI_EEPROM_CC_OFFSET_REG, &reg_val);
	if (ret)
		goto out_relock;

	chip->cc_offset = (s8)reg_val;

	dev_dbg(chip->dev, "cc-offset %d cc-gain %d\n", chip->cc_offset, chip->cc_gain);

out_relock:
	/* Re-lock the EEPROM Access */
	regmap_write(chip->regmap, DC_TI_EEPROM_ACCESS_CONTROL, EEPROM_LOCK);
out:
	if (ret)
		dev_err(chip->dev, "IO-error %d initializing PMIC\n", ret);

	return ret;
}

static int dc_ti_battery_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev->parent);
	struct power_supply_config psy_cfg = {};
	struct fwnode_reference_args args;
	struct gpio_desc *charge_finished;
	struct dc_ti_battery_chip *chip;
	int ret;

	/* On most devices with a Dollar Cove TI the battery is handled by ACPI */
	if (!acpi_quirk_skip_acpi_ac_and_battery())
		return -ENODEV;

	/* ACPI glue code adds a "monitored-battery" fwnode, wait for this */
	ret = fwnode_property_get_reference_args(dev_fwnode(dev), "monitored-battery",
						 NULL, 0, 0, &args);
	if (ret) {
		dev_dbg(dev, "fwnode_property_get_ref() ret %d\n", ret);
		return dev_err_probe(dev, -EPROBE_DEFER, "Waiting for monitored-battery fwnode\n");
	}

	fwnode_handle_put(args.fwnode);

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	chip->regmap = pmic->regmap;

	chip->vbat_channel = devm_iio_channel_get(dev, "VBAT");
	if (IS_ERR(chip->vbat_channel)) {
		dev_dbg(dev, "devm_iio_channel_get() ret %ld\n", PTR_ERR(chip->vbat_channel));
		return dev_err_probe(dev, -EPROBE_DEFER, "Waiting for VBAT IIO channel\n");
	}

	charge_finished = devm_gpiod_get_optional(dev, "charged", GPIOD_IN);
	if (IS_ERR(charge_finished))
		return dev_err_probe(dev, PTR_ERR(charge_finished), "Getting charged GPIO\n");

	ret = dc_ti_battery_hw_init(chip);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, chip);

	psy_cfg.drv_data = chip;
	chip->psy = devm_power_supply_register(dev, &dc_ti_battery_psy_desc, &psy_cfg);
	if (IS_ERR(chip->psy))
		return PTR_ERR(chip->psy);

	return adc_battery_helper_init(&chip->helper, chip->psy,
				       dc_ti_battery_get_voltage_and_current_now,
				       charge_finished);
}

static DEFINE_RUNTIME_DEV_PM_OPS(dc_ti_battery_pm_ops, adc_battery_helper_suspend,
				 adc_battery_helper_resume, NULL);

static struct platform_driver dc_ti_battery_driver = {
	.driver = {
		.name = DEV_NAME,
		.pm = pm_sleep_ptr(&dc_ti_battery_pm_ops),
	},
	.probe = dc_ti_battery_probe,
};
module_platform_driver(dc_ti_battery_driver);

MODULE_ALIAS("platform:" DEV_NAME);
MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("Intel Dollar Cove (TI) battery driver");
MODULE_LICENSE("GPL");
