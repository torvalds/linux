// SPDX-License-Identifier: GPL-2.0+
//
// max14577.c - mfd core driver for the Maxim 14577/77836
//
// Copyright (C) 2014 Samsung Electronics
// Chanwoo Choi <cw00.choi@samsung.com>
// Krzysztof Kozlowski <krzk@kernel.org>
//
// This driver is based on max8997.c

#include <linux/err.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max14577.h>
#include <linux/mfd/max14577-private.h>

/*
 * Table of valid charger currents for different Maxim chipsets.
 * It is placed here because it is used by both charger and regulator driver.
 */
const struct maxim_charger_current maxim_charger_currents[] = {
	[MAXIM_DEVICE_TYPE_UNKNOWN] = { 0, 0, 0, 0 },
	[MAXIM_DEVICE_TYPE_MAX14577] = {
		.min		= MAX14577_CHARGER_CURRENT_LIMIT_MIN,
		.high_start	= MAX14577_CHARGER_CURRENT_LIMIT_HIGH_START,
		.high_step	= MAX14577_CHARGER_CURRENT_LIMIT_HIGH_STEP,
		.max		= MAX14577_CHARGER_CURRENT_LIMIT_MAX,
	},
	[MAXIM_DEVICE_TYPE_MAX77836] = {
		.min		= MAX77836_CHARGER_CURRENT_LIMIT_MIN,
		.high_start	= MAX77836_CHARGER_CURRENT_LIMIT_HIGH_START,
		.high_step	= MAX77836_CHARGER_CURRENT_LIMIT_HIGH_STEP,
		.max		= MAX77836_CHARGER_CURRENT_LIMIT_MAX,
	},
};
EXPORT_SYMBOL_GPL(maxim_charger_currents);

/*
 * maxim_charger_calc_reg_current - Calculate register value for current
 * @limits:	constraints for charger, matching the MBCICHWRC register
 * @min_ua:	minimal requested current, micro Amps
 * @max_ua:	maximum requested current, micro Amps
 * @dst:	destination to store calculated register value
 *
 * Calculates the value of MBCICHWRC (Fast Battery Charge Current) register
 * for given current and stores it under pointed 'dst'. The stored value
 * combines low bit (MBCICHWRCL) and high bits (MBCICHWRCH). It is also
 * properly shifted.
 *
 * The calculated register value matches the current which:
 *  - is always between <limits.min, limits.max>;
 *  - is always less or equal to max_ua;
 *  - is the highest possible value;
 *  - may be lower than min_ua.
 *
 * On success returns 0. On error returns -EINVAL (requested min/max current
 * is outside of given charger limits) and 'dst' is not set.
 */
int maxim_charger_calc_reg_current(const struct maxim_charger_current *limits,
		unsigned int min_ua, unsigned int max_ua, u8 *dst)
{
	unsigned int current_bits = 0xf;

	if (min_ua > max_ua)
		return -EINVAL;

	if (min_ua > limits->max || max_ua < limits->min)
		return -EINVAL;

	if (max_ua < limits->high_start) {
		/*
		 * Less than high_start, so set the minimal current
		 * (turn Low Bit off, 0 as high bits).
		 */
		*dst = 0x0;
		return 0;
	}

	/* max_ua is in range: <high_start, infinite>, cut it to limits.max */
	max_ua = min(limits->max, max_ua);
	max_ua -= limits->high_start;
	/*
	 * There is no risk of overflow 'max_ua' here because:
	 *  - max_ua >= limits.high_start
	 *  - BUILD_BUG checks that 'limits' are: max >= high_start + high_step
	 */
	current_bits = max_ua / limits->high_step;

	/* Turn Low Bit on (use range <limits.high_start, limits.max>) ... */
	*dst = 0x1 << CHGCTRL4_MBCICHWRCL_SHIFT;
	/* and set proper High Bits */
	*dst |= current_bits << CHGCTRL4_MBCICHWRCH_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(maxim_charger_calc_reg_current);

static const struct mfd_cell max14577_devs[] = {
	{
		.name = "max14577-muic",
		.of_compatible = "maxim,max14577-muic",
	},
	{
		.name = "max14577-regulator",
		.of_compatible = "maxim,max14577-regulator",
	},
	{
		.name = "max14577-charger",
		.of_compatible = "maxim,max14577-charger",
	},
};

static const struct mfd_cell max77836_devs[] = {
	{
		.name = "max77836-muic",
		.of_compatible = "maxim,max77836-muic",
	},
	{
		.name = "max77836-regulator",
		.of_compatible = "maxim,max77836-regulator",
	},
	{
		.name = "max77836-charger",
		.of_compatible = "maxim,max77836-charger",
	},
	{
		.name = "max77836-battery",
		.of_compatible = "maxim,max77836-battery",
	},
};

static const struct of_device_id max14577_dt_match[] = {
	{
		.compatible = "maxim,max14577",
		.data = (void *)MAXIM_DEVICE_TYPE_MAX14577,
	},
	{
		.compatible = "maxim,max77836",
		.data = (void *)MAXIM_DEVICE_TYPE_MAX77836,
	},
	{},
};

static bool max14577_muic_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX14577_REG_INT1 ... MAX14577_REG_STATUS3:
		return true;
	default:
		break;
	}
	return false;
}

static bool max77836_muic_volatile_reg(struct device *dev, unsigned int reg)
{
	/* Any max14577 volatile registers are also max77836 volatile. */
	if (max14577_muic_volatile_reg(dev, reg))
		return true;

	switch (reg) {
	case MAX77836_FG_REG_VCELL_MSB ... MAX77836_FG_REG_SOC_LSB:
	case MAX77836_FG_REG_CRATE_MSB ... MAX77836_FG_REG_CRATE_LSB:
	case MAX77836_FG_REG_STATUS_H ... MAX77836_FG_REG_STATUS_L:
	case MAX77836_PMIC_REG_INTSRC:
	case MAX77836_PMIC_REG_TOPSYS_INT:
	case MAX77836_PMIC_REG_TOPSYS_STAT:
		return true;
	default:
		break;
	}
	return false;
}

static const struct regmap_config max14577_muic_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.volatile_reg	= max14577_muic_volatile_reg,
	.max_register	= MAX14577_REG_END,
};

static const struct regmap_config max77836_pmic_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.volatile_reg	= max77836_muic_volatile_reg,
	.max_register	= MAX77836_PMIC_REG_END,
};

static const struct regmap_irq max14577_irqs[] = {
	/* INT1 interrupts */
	{ .reg_offset = 0, .mask = MAX14577_INT1_ADC_MASK, },
	{ .reg_offset = 0, .mask = MAX14577_INT1_ADCLOW_MASK, },
	{ .reg_offset = 0, .mask = MAX14577_INT1_ADCERR_MASK, },
	/* INT2 interrupts */
	{ .reg_offset = 1, .mask = MAX14577_INT2_CHGTYP_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_CHGDETRUN_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_DCDTMR_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_DBCHG_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_VBVOLT_MASK, },
	/* INT3 interrupts */
	{ .reg_offset = 2, .mask = MAX14577_INT3_EOC_MASK, },
	{ .reg_offset = 2, .mask = MAX14577_INT3_CGMBC_MASK, },
	{ .reg_offset = 2, .mask = MAX14577_INT3_OVP_MASK, },
	{ .reg_offset = 2, .mask = MAX14577_INT3_MBCCHGERR_MASK, },
};

static const struct regmap_irq_chip max14577_irq_chip = {
	.name			= "max14577",
	.status_base		= MAX14577_REG_INT1,
	.mask_base		= MAX14577_REG_INTMASK1,
	.mask_invert		= true,
	.num_regs		= 3,
	.irqs			= max14577_irqs,
	.num_irqs		= ARRAY_SIZE(max14577_irqs),
};

static const struct regmap_irq max77836_muic_irqs[] = {
	/* INT1 interrupts */
	{ .reg_offset = 0, .mask = MAX14577_INT1_ADC_MASK, },
	{ .reg_offset = 0, .mask = MAX14577_INT1_ADCLOW_MASK, },
	{ .reg_offset = 0, .mask = MAX14577_INT1_ADCERR_MASK, },
	{ .reg_offset = 0, .mask = MAX77836_INT1_ADC1K_MASK, },
	/* INT2 interrupts */
	{ .reg_offset = 1, .mask = MAX14577_INT2_CHGTYP_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_CHGDETRUN_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_DCDTMR_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_DBCHG_MASK, },
	{ .reg_offset = 1, .mask = MAX14577_INT2_VBVOLT_MASK, },
	{ .reg_offset = 1, .mask = MAX77836_INT2_VIDRM_MASK, },
	/* INT3 interrupts */
	{ .reg_offset = 2, .mask = MAX14577_INT3_EOC_MASK, },
	{ .reg_offset = 2, .mask = MAX14577_INT3_CGMBC_MASK, },
	{ .reg_offset = 2, .mask = MAX14577_INT3_OVP_MASK, },
	{ .reg_offset = 2, .mask = MAX14577_INT3_MBCCHGERR_MASK, },
};

static const struct regmap_irq_chip max77836_muic_irq_chip = {
	.name			= "max77836-muic",
	.status_base		= MAX14577_REG_INT1,
	.mask_base		= MAX14577_REG_INTMASK1,
	.mask_invert		= true,
	.num_regs		= 3,
	.irqs			= max77836_muic_irqs,
	.num_irqs		= ARRAY_SIZE(max77836_muic_irqs),
};

static const struct regmap_irq max77836_pmic_irqs[] = {
	{ .reg_offset = 0, .mask = MAX77836_TOPSYS_INT_T120C_MASK, },
	{ .reg_offset = 0, .mask = MAX77836_TOPSYS_INT_T140C_MASK, },
};

static const struct regmap_irq_chip max77836_pmic_irq_chip = {
	.name			= "max77836-pmic",
	.status_base		= MAX77836_PMIC_REG_TOPSYS_INT,
	.mask_base		= MAX77836_PMIC_REG_TOPSYS_INT_MASK,
	.mask_invert		= false,
	.num_regs		= 1,
	.irqs			= max77836_pmic_irqs,
	.num_irqs		= ARRAY_SIZE(max77836_pmic_irqs),
};

static void max14577_print_dev_type(struct max14577 *max14577)
{
	u8 reg_data, vendor_id, device_id;
	int ret;

	ret = max14577_read_reg(max14577->regmap, MAX14577_REG_DEVICEID,
			&reg_data);
	if (ret) {
		dev_err(max14577->dev,
			"Failed to read DEVICEID register: %d\n", ret);
		return;
	}

	vendor_id = ((reg_data & DEVID_VENDORID_MASK) >>
				DEVID_VENDORID_SHIFT);
	device_id = ((reg_data & DEVID_DEVICEID_MASK) >>
				DEVID_DEVICEID_SHIFT);

	dev_info(max14577->dev, "Device type: %u (ID: 0x%x, vendor: 0x%x)\n",
			max14577->dev_type, device_id, vendor_id);
}

/*
 * Max77836 specific initialization code for driver probe.
 * Adds new I2C dummy device, regmap and regmap IRQ chip.
 * Unmasks Interrupt Source register.
 *
 * On success returns 0.
 * On failure returns errno and reverts any changes done so far (e.g. remove
 * I2C dummy device), except masking the INT SRC register.
 */
static int max77836_init(struct max14577 *max14577)
{
	int ret;
	u8 intsrc_mask;

	max14577->i2c_pmic = i2c_new_dummy(max14577->i2c->adapter,
			I2C_ADDR_PMIC);
	if (!max14577->i2c_pmic) {
		dev_err(max14577->dev, "Failed to register PMIC I2C device\n");
		return -ENODEV;
	}
	i2c_set_clientdata(max14577->i2c_pmic, max14577);

	max14577->regmap_pmic = devm_regmap_init_i2c(max14577->i2c_pmic,
			&max77836_pmic_regmap_config);
	if (IS_ERR(max14577->regmap_pmic)) {
		ret = PTR_ERR(max14577->regmap_pmic);
		dev_err(max14577->dev, "Failed to allocate PMIC register map: %d\n",
				ret);
		goto err;
	}

	/* Un-mask MAX77836 Interrupt Source register */
	ret = max14577_read_reg(max14577->regmap_pmic,
			MAX77836_PMIC_REG_INTSRC_MASK, &intsrc_mask);
	if (ret < 0) {
		dev_err(max14577->dev, "Failed to read PMIC register\n");
		goto err;
	}

	intsrc_mask &= ~(MAX77836_INTSRC_MASK_TOP_INT_MASK);
	intsrc_mask &= ~(MAX77836_INTSRC_MASK_MUIC_CHG_INT_MASK);
	ret = max14577_write_reg(max14577->regmap_pmic,
			MAX77836_PMIC_REG_INTSRC_MASK, intsrc_mask);
	if (ret < 0) {
		dev_err(max14577->dev, "Failed to write PMIC register\n");
		goto err;
	}

	ret = regmap_add_irq_chip(max14577->regmap_pmic, max14577->irq,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_SHARED,
			0, &max77836_pmic_irq_chip,
			&max14577->irq_data_pmic);
	if (ret != 0) {
		dev_err(max14577->dev, "Failed to request PMIC IRQ %d: %d\n",
				max14577->irq, ret);
		goto err;
	}

	return 0;

err:
	i2c_unregister_device(max14577->i2c_pmic);

	return ret;
}

/*
 * Max77836 specific de-initialization code for driver remove.
 */
static void max77836_remove(struct max14577 *max14577)
{
	regmap_del_irq_chip(max14577->irq, max14577->irq_data_pmic);
	i2c_unregister_device(max14577->i2c_pmic);
}

static int max14577_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct max14577 *max14577;
	struct max14577_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct device_node *np = i2c->dev.of_node;
	int ret = 0;
	const struct regmap_irq_chip *irq_chip;
	const struct mfd_cell *mfd_devs;
	unsigned int mfd_devs_size;
	int irq_flags;

	if (np) {
		pdata = devm_kzalloc(&i2c->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		i2c->dev.platform_data = pdata;
	}

	if (!pdata) {
		dev_err(&i2c->dev, "No platform data found.\n");
		return -EINVAL;
	}

	max14577 = devm_kzalloc(&i2c->dev, sizeof(*max14577), GFP_KERNEL);
	if (!max14577)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max14577);
	max14577->dev = &i2c->dev;
	max14577->i2c = i2c;
	max14577->irq = i2c->irq;

	max14577->regmap = devm_regmap_init_i2c(i2c,
			&max14577_muic_regmap_config);
	if (IS_ERR(max14577->regmap)) {
		ret = PTR_ERR(max14577->regmap);
		dev_err(max14577->dev, "Failed to allocate register map: %d\n",
				ret);
		return ret;
	}

	if (np) {
		const struct of_device_id *of_id;

		of_id = of_match_device(max14577_dt_match, &i2c->dev);
		if (of_id)
			max14577->dev_type =
				(enum maxim_device_type)of_id->data;
	} else {
		max14577->dev_type = id->driver_data;
	}

	max14577_print_dev_type(max14577);

	switch (max14577->dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		irq_chip = &max77836_muic_irq_chip;
		mfd_devs = max77836_devs;
		mfd_devs_size = ARRAY_SIZE(max77836_devs);
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_SHARED;
		break;
	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		irq_chip = &max14577_irq_chip;
		mfd_devs = max14577_devs;
		mfd_devs_size = ARRAY_SIZE(max14577_devs);
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
		break;
	}

	ret = regmap_add_irq_chip(max14577->regmap, max14577->irq,
				  irq_flags, 0, irq_chip,
				  &max14577->irq_data);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request IRQ %d: %d\n",
				max14577->irq, ret);
		return ret;
	}

	/* Max77836 specific initialization code (additional regmap) */
	if (max14577->dev_type == MAXIM_DEVICE_TYPE_MAX77836) {
		ret = max77836_init(max14577);
		if (ret < 0)
			goto err_max77836;
	}

	ret = mfd_add_devices(max14577->dev, -1, mfd_devs,
			mfd_devs_size, NULL, 0, NULL);
	if (ret < 0)
		goto err_mfd;

	device_init_wakeup(max14577->dev, 1);

	return 0;

err_mfd:
	if (max14577->dev_type == MAXIM_DEVICE_TYPE_MAX77836)
		max77836_remove(max14577);
err_max77836:
	regmap_del_irq_chip(max14577->irq, max14577->irq_data);

	return ret;
}

static int max14577_i2c_remove(struct i2c_client *i2c)
{
	struct max14577 *max14577 = i2c_get_clientdata(i2c);

	mfd_remove_devices(max14577->dev);
	regmap_del_irq_chip(max14577->irq, max14577->irq_data);
	if (max14577->dev_type == MAXIM_DEVICE_TYPE_MAX77836)
		max77836_remove(max14577);

	return 0;
}

static const struct i2c_device_id max14577_i2c_id[] = {
	{ "max14577", MAXIM_DEVICE_TYPE_MAX14577, },
	{ "max77836", MAXIM_DEVICE_TYPE_MAX77836, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max14577_i2c_id);

#ifdef CONFIG_PM_SLEEP
static int max14577_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max14577 *max14577 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		enable_irq_wake(max14577->irq);
	/*
	 * MUIC IRQ must be disabled during suspend because if it happens
	 * while suspended it will be handled before resuming I2C.
	 *
	 * When device is woken up from suspend (e.g. by ADC change),
	 * an interrupt occurs before resuming I2C bus controller.
	 * Interrupt handler tries to read registers but this read
	 * will fail because I2C is still suspended.
	 */
	disable_irq(max14577->irq);

	return 0;
}

static int max14577_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max14577 *max14577 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(max14577->irq);
	enable_irq(max14577->irq);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max14577_pm, max14577_suspend, max14577_resume);

static struct i2c_driver max14577_i2c_driver = {
	.driver = {
		.name = "max14577",
		.pm = &max14577_pm,
		.of_match_table = max14577_dt_match,
	},
	.probe = max14577_i2c_probe,
	.remove = max14577_i2c_remove,
	.id_table = max14577_i2c_id,
};

static int __init max14577_i2c_init(void)
{
	BUILD_BUG_ON(ARRAY_SIZE(max14577_i2c_id) != MAXIM_DEVICE_TYPE_NUM);
	BUILD_BUG_ON(ARRAY_SIZE(max14577_dt_match) != MAXIM_DEVICE_TYPE_NUM);

	/* Valid charger current values must be provided for each chipset */
	BUILD_BUG_ON(ARRAY_SIZE(maxim_charger_currents) != MAXIM_DEVICE_TYPE_NUM);

	/* Check for valid values for charger */
	BUILD_BUG_ON(MAX14577_CHARGER_CURRENT_LIMIT_HIGH_START +
			MAX14577_CHARGER_CURRENT_LIMIT_HIGH_STEP * 0xf !=
			MAX14577_CHARGER_CURRENT_LIMIT_MAX);
	BUILD_BUG_ON(MAX14577_CHARGER_CURRENT_LIMIT_HIGH_STEP == 0);

	BUILD_BUG_ON(MAX77836_CHARGER_CURRENT_LIMIT_HIGH_START +
			MAX77836_CHARGER_CURRENT_LIMIT_HIGH_STEP * 0xf !=
			MAX77836_CHARGER_CURRENT_LIMIT_MAX);
	BUILD_BUG_ON(MAX77836_CHARGER_CURRENT_LIMIT_HIGH_STEP == 0);

	return i2c_add_driver(&max14577_i2c_driver);
}
module_init(max14577_i2c_init);

static void __exit max14577_i2c_exit(void)
{
	i2c_del_driver(&max14577_i2c_driver);
}
module_exit(max14577_i2c_exit);

MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>, Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_DESCRIPTION("Maxim 14577/77836 multi-function core driver");
MODULE_LICENSE("GPL");
