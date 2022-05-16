// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2019 ROHM Semiconductors
//
// ROHM BD71828 PMIC driver

#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rohm-bd71828.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

static struct gpio_keys_button button = {
	.code = KEY_POWER,
	.gpio = -1,
	.type = EV_KEY,
};

static struct gpio_keys_platform_data bd71828_powerkey_data = {
	.buttons = &button,
	.nbuttons = 1,
	.name = "bd71828-pwrkey",
};

static const struct resource rtc_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD71828_INT_RTC0, "bd71828-rtc-alm-0"),
	DEFINE_RES_IRQ_NAMED(BD71828_INT_RTC1, "bd71828-rtc-alm-1"),
	DEFINE_RES_IRQ_NAMED(BD71828_INT_RTC2, "bd71828-rtc-alm-2"),
};

static struct mfd_cell bd71828_mfd_cells[] = {
	{ .name = "bd71828-pmic", },
	{ .name = "bd71828-gpio", },
	{ .name = "bd71828-led", .of_compatible = "rohm,bd71828-leds" },
	/*
	 * We use BD71837 driver to drive the clock block. Only differences to
	 * BD70528 clock gate are the register address and mask.
	 */
	{ .name = "bd71828-clk", },
	{ .name = "bd71827-power", },
	{
		.name = "bd71828-rtc",
		.resources = rtc_irqs,
		.num_resources = ARRAY_SIZE(rtc_irqs),
	}, {
		.name = "gpio-keys",
		.platform_data = &bd71828_powerkey_data,
		.pdata_size = sizeof(bd71828_powerkey_data),
	},
};

static const struct regmap_range volatile_ranges[] = {
	{
		.range_min = BD71828_REG_PS_CTRL_1,
		.range_max = BD71828_REG_PS_CTRL_1,
	}, {
		.range_min = BD71828_REG_PS_CTRL_3,
		.range_max = BD71828_REG_PS_CTRL_3,
	}, {
		.range_min = BD71828_REG_RTC_SEC,
		.range_max = BD71828_REG_RTC_YEAR,
	}, {
		/*
		 * For now make all charger registers volatile because many
		 * needs to be and because the charger block is not that
		 * performance critical.
		 */
		.range_min = BD71828_REG_CHG_STATE,
		.range_max = BD71828_REG_CHG_FULL,
	}, {
		.range_min = BD71828_REG_INT_MAIN,
		.range_max = BD71828_REG_IO_STAT,
	},
};

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = &volatile_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(volatile_ranges),
};

static struct regmap_config bd71828_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.max_register = BD71828_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * Mapping of main IRQ register bits to sub-IRQ register offsets so that we can
 * access corect sub-IRQ registers based on bits that are set in main IRQ
 * register.
 */

static unsigned int bit0_offsets[] = {11};		/* RTC IRQ */
static unsigned int bit1_offsets[] = {10};		/* TEMP IRQ */
static unsigned int bit2_offsets[] = {6, 7, 8, 9};	/* BAT MON IRQ */
static unsigned int bit3_offsets[] = {5};		/* BAT IRQ */
static unsigned int bit4_offsets[] = {4};		/* CHG IRQ */
static unsigned int bit5_offsets[] = {3};		/* VSYS IRQ */
static unsigned int bit6_offsets[] = {1, 2};		/* DCIN IRQ */
static unsigned int bit7_offsets[] = {0};		/* BUCK IRQ */

static struct regmap_irq_sub_irq_map bd71828_sub_irq_offsets[] = {
	REGMAP_IRQ_MAIN_REG_OFFSET(bit0_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit1_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit2_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit3_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit4_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit5_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit6_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit7_offsets),
};

static struct regmap_irq bd71828_irqs[] = {
	REGMAP_IRQ_REG(BD71828_INT_BUCK1_OCP, 0, BD71828_INT_BUCK1_OCP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BUCK2_OCP, 0, BD71828_INT_BUCK2_OCP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BUCK3_OCP, 0, BD71828_INT_BUCK3_OCP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BUCK4_OCP, 0, BD71828_INT_BUCK4_OCP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BUCK5_OCP, 0, BD71828_INT_BUCK5_OCP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BUCK6_OCP, 0, BD71828_INT_BUCK6_OCP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BUCK7_OCP, 0, BD71828_INT_BUCK7_OCP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_PGFAULT, 0, BD71828_INT_PGFAULT_MASK),
	/* DCIN1 interrupts */
	REGMAP_IRQ_REG(BD71828_INT_DCIN_DET, 1, BD71828_INT_DCIN_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_DCIN_RMV, 1, BD71828_INT_DCIN_RMV_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CLPS_OUT, 1, BD71828_INT_CLPS_OUT_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CLPS_IN, 1, BD71828_INT_CLPS_IN_MASK),
	/* DCIN2 interrupts */
	REGMAP_IRQ_REG(BD71828_INT_DCIN_MON_RES, 2,
		       BD71828_INT_DCIN_MON_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_DCIN_MON_DET, 2,
		       BD71828_INT_DCIN_MON_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_LONGPUSH, 2, BD71828_INT_LONGPUSH_MASK),
	REGMAP_IRQ_REG(BD71828_INT_MIDPUSH, 2, BD71828_INT_MIDPUSH_MASK),
	REGMAP_IRQ_REG(BD71828_INT_SHORTPUSH, 2, BD71828_INT_SHORTPUSH_MASK),
	REGMAP_IRQ_REG(BD71828_INT_PUSH, 2, BD71828_INT_PUSH_MASK),
	REGMAP_IRQ_REG(BD71828_INT_WDOG, 2, BD71828_INT_WDOG_MASK),
	REGMAP_IRQ_REG(BD71828_INT_SWRESET, 2, BD71828_INT_SWRESET_MASK),
	/* Vsys */
	REGMAP_IRQ_REG(BD71828_INT_VSYS_UV_RES, 3,
		       BD71828_INT_VSYS_UV_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_VSYS_UV_DET, 3,
		       BD71828_INT_VSYS_UV_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_VSYS_LOW_RES, 3,
		       BD71828_INT_VSYS_LOW_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_VSYS_LOW_DET, 3,
		       BD71828_INT_VSYS_LOW_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_VSYS_HALL_IN, 3,
		       BD71828_INT_VSYS_HALL_IN_MASK),
	REGMAP_IRQ_REG(BD71828_INT_VSYS_HALL_TOGGLE, 3,
		       BD71828_INT_VSYS_HALL_TOGGLE_MASK),
	REGMAP_IRQ_REG(BD71828_INT_VSYS_MON_RES, 3,
		       BD71828_INT_VSYS_MON_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_VSYS_MON_DET, 3,
		       BD71828_INT_VSYS_MON_DET_MASK),
	/* Charger */
	REGMAP_IRQ_REG(BD71828_INT_CHG_DCIN_ILIM, 4,
		       BD71828_INT_CHG_DCIN_ILIM_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CHG_TOPOFF_TO_DONE, 4,
		       BD71828_INT_CHG_TOPOFF_TO_DONE_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CHG_WDG_TEMP, 4,
		       BD71828_INT_CHG_WDG_TEMP_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CHG_WDG_TIME, 4,
		       BD71828_INT_CHG_WDG_TIME_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CHG_RECHARGE_RES, 4,
		       BD71828_INT_CHG_RECHARGE_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CHG_RECHARGE_DET, 4,
		       BD71828_INT_CHG_RECHARGE_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CHG_RANGED_TEMP_TRANSITION, 4,
		       BD71828_INT_CHG_RANGED_TEMP_TRANSITION_MASK),
	REGMAP_IRQ_REG(BD71828_INT_CHG_STATE_TRANSITION, 4,
		       BD71828_INT_CHG_STATE_TRANSITION_MASK),
	/* Battery */
	REGMAP_IRQ_REG(BD71828_INT_BAT_TEMP_NORMAL, 5,
		       BD71828_INT_BAT_TEMP_NORMAL_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_TEMP_ERANGE, 5,
		       BD71828_INT_BAT_TEMP_ERANGE_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_TEMP_WARN, 5,
		       BD71828_INT_BAT_TEMP_WARN_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_REMOVED, 5,
		       BD71828_INT_BAT_REMOVED_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_DETECTED, 5,
		       BD71828_INT_BAT_DETECTED_MASK),
	REGMAP_IRQ_REG(BD71828_INT_THERM_REMOVED, 5,
		       BD71828_INT_THERM_REMOVED_MASK),
	REGMAP_IRQ_REG(BD71828_INT_THERM_DETECTED, 5,
		       BD71828_INT_THERM_DETECTED_MASK),
	/* Battery Mon 1 */
	REGMAP_IRQ_REG(BD71828_INT_BAT_DEAD, 6, BD71828_INT_BAT_DEAD_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_SHORTC_RES, 6,
		       BD71828_INT_BAT_SHORTC_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_SHORTC_DET, 6,
		       BD71828_INT_BAT_SHORTC_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_LOW_VOLT_RES, 6,
		       BD71828_INT_BAT_LOW_VOLT_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_LOW_VOLT_DET, 6,
		       BD71828_INT_BAT_LOW_VOLT_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_VOLT_RES, 6,
		       BD71828_INT_BAT_OVER_VOLT_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_VOLT_DET, 6,
		       BD71828_INT_BAT_OVER_VOLT_DET_MASK),
	/* Battery Mon 2 */
	REGMAP_IRQ_REG(BD71828_INT_BAT_MON_RES, 7,
		       BD71828_INT_BAT_MON_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_MON_DET, 7,
		       BD71828_INT_BAT_MON_DET_MASK),
	/* Battery Mon 3 (Coulomb counter) */
	REGMAP_IRQ_REG(BD71828_INT_BAT_CC_MON1, 8,
		       BD71828_INT_BAT_CC_MON1_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_CC_MON2, 8,
		       BD71828_INT_BAT_CC_MON2_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_CC_MON3, 8,
		       BD71828_INT_BAT_CC_MON3_MASK),
	/* Battery Mon 4 */
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_CURR_1_RES, 9,
		       BD71828_INT_BAT_OVER_CURR_1_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_CURR_1_DET, 9,
		       BD71828_INT_BAT_OVER_CURR_1_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_CURR_2_RES, 9,
		       BD71828_INT_BAT_OVER_CURR_2_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_CURR_2_DET, 9,
		       BD71828_INT_BAT_OVER_CURR_2_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_CURR_3_RES, 9,
		       BD71828_INT_BAT_OVER_CURR_3_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_BAT_OVER_CURR_3_DET, 9,
		       BD71828_INT_BAT_OVER_CURR_3_DET_MASK),
	/* Temperature */
	REGMAP_IRQ_REG(BD71828_INT_TEMP_BAT_LOW_RES, 10,
		       BD71828_INT_TEMP_BAT_LOW_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_TEMP_BAT_LOW_DET, 10,
		       BD71828_INT_TEMP_BAT_LOW_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_TEMP_BAT_HI_RES, 10,
		       BD71828_INT_TEMP_BAT_HI_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_TEMP_BAT_HI_DET, 10,
		       BD71828_INT_TEMP_BAT_HI_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_TEMP_CHIP_OVER_125_RES, 10,
		       BD71828_INT_TEMP_CHIP_OVER_125_RES_MASK),
	REGMAP_IRQ_REG(BD71828_INT_TEMP_CHIP_OVER_125_DET, 10,
		       BD71828_INT_TEMP_CHIP_OVER_125_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_TEMP_CHIP_OVER_VF_DET, 10,
		       BD71828_INT_TEMP_CHIP_OVER_VF_DET_MASK),
	REGMAP_IRQ_REG(BD71828_INT_TEMP_CHIP_OVER_VF_RES, 10,
		       BD71828_INT_TEMP_CHIP_OVER_VF_RES_MASK),
	/* RTC Alarm */
	REGMAP_IRQ_REG(BD71828_INT_RTC0, 11, BD71828_INT_RTC0_MASK),
	REGMAP_IRQ_REG(BD71828_INT_RTC1, 11, BD71828_INT_RTC1_MASK),
	REGMAP_IRQ_REG(BD71828_INT_RTC2, 11, BD71828_INT_RTC2_MASK),
};

static struct regmap_irq_chip bd71828_irq_chip = {
	.name = "bd71828_irq",
	.main_status = BD71828_REG_INT_MAIN,
	.irqs = &bd71828_irqs[0],
	.num_irqs = ARRAY_SIZE(bd71828_irqs),
	.status_base = BD71828_REG_INT_BUCK,
	.mask_base = BD71828_REG_INT_MASK_BUCK,
	.ack_base = BD71828_REG_INT_BUCK,
	.mask_invert = true,
	.init_ack_masked = true,
	.num_regs = 12,
	.num_main_regs = 1,
	.sub_reg_offsets = &bd71828_sub_irq_offsets[0],
	.num_main_status_bits = 8,
	.irq_reg_stride = 1,
};

static int bd71828_i2c_probe(struct i2c_client *i2c)
{
	struct rohm_regmap_dev *chip;
	struct regmap_irq_chip_data *irq_data;
	int ret;

	if (!i2c->irq) {
		dev_err(&i2c->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	chip = devm_kzalloc(&i2c->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	dev_set_drvdata(&i2c->dev, chip);

	chip->regmap = devm_regmap_init_i2c(i2c, &bd71828_regmap);
	if (IS_ERR(chip->regmap)) {
		dev_err(&i2c->dev, "Failed to initialize Regmap\n");
		return PTR_ERR(chip->regmap);
	}

	ret = devm_regmap_add_irq_chip(&i2c->dev, chip->regmap,
				       i2c->irq, IRQF_ONESHOT, 0,
				       &bd71828_irq_chip, &irq_data);
	if (ret) {
		dev_err(&i2c->dev, "Failed to add IRQ chip\n");
		return ret;
	}

	dev_dbg(&i2c->dev, "Registered %d IRQs for chip\n",
		bd71828_irq_chip.num_irqs);

	ret = regmap_irq_get_virq(irq_data, BD71828_INT_SHORTPUSH);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to get the power-key IRQ\n");
		return ret;
	}

	button.irq = ret;

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO,
				   bd71828_mfd_cells,
				   ARRAY_SIZE(bd71828_mfd_cells), NULL, 0,
				   regmap_irq_get_domain(irq_data));
	if (ret)
		dev_err(&i2c->dev, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd71828_of_match[] = {
	{ .compatible = "rohm,bd71828", },
	{ },
};
MODULE_DEVICE_TABLE(of, bd71828_of_match);

static struct i2c_driver bd71828_drv = {
	.driver = {
		.name = "rohm-bd71828",
		.of_match_table = bd71828_of_match,
	},
	.probe_new = &bd71828_i2c_probe,
};
module_i2c_driver(bd71828_drv);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD71828 Power Management IC driver");
MODULE_LICENSE("GPL");
