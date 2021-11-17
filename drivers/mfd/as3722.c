// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Core driver for ams AS3722 PMICs
 *
 * Copyright (C) 2013 AMS AG
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 *
 * Author: Florian Lobmaier <florian.lobmaier@ams.com>
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/as3722.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define AS3722_DEVICE_ID	0x0C

static const struct resource as3722_rtc_resource[] = {
	DEFINE_RES_IRQ_NAMED(AS3722_IRQ_RTC_ALARM, "as3722-rtc-alarm"),
};

static const struct resource as3722_adc_resource[] = {
	DEFINE_RES_IRQ_NAMED(AS3722_IRQ_ADC, "as3722-adc"),
};

static const struct mfd_cell as3722_devs[] = {
	{
		.name = "as3722-pinctrl",
	},
	{
		.name = "as3722-regulator",
	},
	{
		.name = "as3722-rtc",
		.num_resources = ARRAY_SIZE(as3722_rtc_resource),
		.resources = as3722_rtc_resource,
	},
	{
		.name = "as3722-adc",
		.num_resources = ARRAY_SIZE(as3722_adc_resource),
		.resources = as3722_adc_resource,
	},
	{
		.name = "as3722-power-off",
	},
	{
		.name = "as3722-wdt",
	},
};

static const struct regmap_irq as3722_irqs[] = {
	/* INT1 IRQs */
	[AS3722_IRQ_LID] = {
		.mask = AS3722_INTERRUPT_MASK1_LID,
	},
	[AS3722_IRQ_ACOK] = {
		.mask = AS3722_INTERRUPT_MASK1_ACOK,
	},
	[AS3722_IRQ_ENABLE1] = {
		.mask = AS3722_INTERRUPT_MASK1_ENABLE1,
	},
	[AS3722_IRQ_OCCUR_ALARM_SD0] = {
		.mask = AS3722_INTERRUPT_MASK1_OCURR_ALARM_SD0,
	},
	[AS3722_IRQ_ONKEY_LONG_PRESS] = {
		.mask = AS3722_INTERRUPT_MASK1_ONKEY_LONG,
	},
	[AS3722_IRQ_ONKEY] = {
		.mask = AS3722_INTERRUPT_MASK1_ONKEY,
	},
	[AS3722_IRQ_OVTMP] = {
		.mask = AS3722_INTERRUPT_MASK1_OVTMP,
	},
	[AS3722_IRQ_LOWBAT] = {
		.mask = AS3722_INTERRUPT_MASK1_LOWBAT,
	},

	/* INT2 IRQs */
	[AS3722_IRQ_SD0_LV] = {
		.mask = AS3722_INTERRUPT_MASK2_SD0_LV,
		.reg_offset = 1,
	},
	[AS3722_IRQ_SD1_LV] = {
		.mask = AS3722_INTERRUPT_MASK2_SD1_LV,
		.reg_offset = 1,
	},
	[AS3722_IRQ_SD2_LV] = {
		.mask = AS3722_INTERRUPT_MASK2_SD2345_LV,
		.reg_offset = 1,
	},
	[AS3722_IRQ_PWM1_OV_PROT] = {
		.mask = AS3722_INTERRUPT_MASK2_PWM1_OV_PROT,
		.reg_offset = 1,
	},
	[AS3722_IRQ_PWM2_OV_PROT] = {
		.mask = AS3722_INTERRUPT_MASK2_PWM2_OV_PROT,
		.reg_offset = 1,
	},
	[AS3722_IRQ_ENABLE2] = {
		.mask = AS3722_INTERRUPT_MASK2_ENABLE2,
		.reg_offset = 1,
	},
	[AS3722_IRQ_SD6_LV] = {
		.mask = AS3722_INTERRUPT_MASK2_SD6_LV,
		.reg_offset = 1,
	},
	[AS3722_IRQ_RTC_REP] = {
		.mask = AS3722_INTERRUPT_MASK2_RTC_REP,
		.reg_offset = 1,
	},

	/* INT3 IRQs */
	[AS3722_IRQ_RTC_ALARM] = {
		.mask = AS3722_INTERRUPT_MASK3_RTC_ALARM,
		.reg_offset = 2,
	},
	[AS3722_IRQ_GPIO1] = {
		.mask = AS3722_INTERRUPT_MASK3_GPIO1,
		.reg_offset = 2,
	},
	[AS3722_IRQ_GPIO2] = {
		.mask = AS3722_INTERRUPT_MASK3_GPIO2,
		.reg_offset = 2,
	},
	[AS3722_IRQ_GPIO3] = {
		.mask = AS3722_INTERRUPT_MASK3_GPIO3,
		.reg_offset = 2,
	},
	[AS3722_IRQ_GPIO4] = {
		.mask = AS3722_INTERRUPT_MASK3_GPIO4,
		.reg_offset = 2,
	},
	[AS3722_IRQ_GPIO5] = {
		.mask = AS3722_INTERRUPT_MASK3_GPIO5,
		.reg_offset = 2,
	},
	[AS3722_IRQ_WATCHDOG] = {
		.mask = AS3722_INTERRUPT_MASK3_WATCHDOG,
		.reg_offset = 2,
	},
	[AS3722_IRQ_ENABLE3] = {
		.mask = AS3722_INTERRUPT_MASK3_ENABLE3,
		.reg_offset = 2,
	},

	/* INT4 IRQs */
	[AS3722_IRQ_TEMP_SD0_SHUTDOWN] = {
		.mask = AS3722_INTERRUPT_MASK4_TEMP_SD0_SHUTDOWN,
		.reg_offset = 3,
	},
	[AS3722_IRQ_TEMP_SD1_SHUTDOWN] = {
		.mask = AS3722_INTERRUPT_MASK4_TEMP_SD1_SHUTDOWN,
		.reg_offset = 3,
	},
	[AS3722_IRQ_TEMP_SD2_SHUTDOWN] = {
		.mask = AS3722_INTERRUPT_MASK4_TEMP_SD6_SHUTDOWN,
		.reg_offset = 3,
	},
	[AS3722_IRQ_TEMP_SD0_ALARM] = {
		.mask = AS3722_INTERRUPT_MASK4_TEMP_SD0_ALARM,
		.reg_offset = 3,
	},
	[AS3722_IRQ_TEMP_SD1_ALARM] = {
		.mask = AS3722_INTERRUPT_MASK4_TEMP_SD1_ALARM,
		.reg_offset = 3,
	},
	[AS3722_IRQ_TEMP_SD6_ALARM] = {
		.mask = AS3722_INTERRUPT_MASK4_TEMP_SD6_ALARM,
		.reg_offset = 3,
	},
	[AS3722_IRQ_OCCUR_ALARM_SD6] = {
		.mask = AS3722_INTERRUPT_MASK4_OCCUR_ALARM_SD6,
		.reg_offset = 3,
	},
	[AS3722_IRQ_ADC] = {
		.mask = AS3722_INTERRUPT_MASK4_ADC,
		.reg_offset = 3,
	},
};

static const struct regmap_irq_chip as3722_irq_chip = {
	.name = "as3722",
	.irqs = as3722_irqs,
	.num_irqs = ARRAY_SIZE(as3722_irqs),
	.num_regs = 4,
	.status_base = AS3722_INTERRUPT_STATUS1_REG,
	.mask_base = AS3722_INTERRUPT_MASK1_REG,
};

static int as3722_check_device_id(struct as3722 *as3722)
{
	u32 val;
	int ret;

	/* Check that this is actually a AS3722 */
	ret = as3722_read(as3722, AS3722_ASIC_ID1_REG, &val);
	if (ret < 0) {
		dev_err(as3722->dev, "ASIC_ID1 read failed: %d\n", ret);
		return ret;
	}

	if (val != AS3722_DEVICE_ID) {
		dev_err(as3722->dev, "Device is not AS3722, ID is 0x%x\n", val);
		return -ENODEV;
	}

	ret = as3722_read(as3722, AS3722_ASIC_ID2_REG, &val);
	if (ret < 0) {
		dev_err(as3722->dev, "ASIC_ID2 read failed: %d\n", ret);
		return ret;
	}

	dev_info(as3722->dev, "AS3722 with revision 0x%x found\n", val);
	return 0;
}

static int as3722_configure_pullups(struct as3722 *as3722)
{
	int ret;
	u32 val = 0;

	if (as3722->en_intern_int_pullup)
		val |= AS3722_INT_PULL_UP;
	if (as3722->en_intern_i2c_pullup)
		val |= AS3722_I2C_PULL_UP;

	ret = as3722_update_bits(as3722, AS3722_IOVOLTAGE_REG,
			AS3722_INT_PULL_UP | AS3722_I2C_PULL_UP, val);
	if (ret < 0)
		dev_err(as3722->dev, "IOVOLTAGE_REG update failed: %d\n", ret);
	return ret;
}

static const struct regmap_range as3722_readable_ranges[] = {
	regmap_reg_range(AS3722_SD0_VOLTAGE_REG, AS3722_SD6_VOLTAGE_REG),
	regmap_reg_range(AS3722_GPIO0_CONTROL_REG, AS3722_LDO7_VOLTAGE_REG),
	regmap_reg_range(AS3722_LDO9_VOLTAGE_REG, AS3722_REG_SEQU_MOD3_REG),
	regmap_reg_range(AS3722_SD_PHSW_CTRL_REG, AS3722_PWM_CONTROL_H_REG),
	regmap_reg_range(AS3722_WATCHDOG_TIMER_REG, AS3722_WATCHDOG_TIMER_REG),
	regmap_reg_range(AS3722_WATCHDOG_SOFTWARE_SIGNAL_REG,
					AS3722_BATTERY_VOLTAGE_MONITOR2_REG),
	regmap_reg_range(AS3722_SD_CONTROL_REG, AS3722_PWM_VCONTROL4_REG),
	regmap_reg_range(AS3722_BB_CHARGER_REG, AS3722_SRAM_REG),
	regmap_reg_range(AS3722_RTC_ACCESS_REG, AS3722_RTC_ACCESS_REG),
	regmap_reg_range(AS3722_RTC_STATUS_REG, AS3722_TEMP_STATUS_REG),
	regmap_reg_range(AS3722_ADC0_CONTROL_REG, AS3722_ADC_CONFIGURATION_REG),
	regmap_reg_range(AS3722_ASIC_ID1_REG, AS3722_ASIC_ID2_REG),
	regmap_reg_range(AS3722_LOCK_REG, AS3722_LOCK_REG),
	regmap_reg_range(AS3722_FUSE7_REG, AS3722_FUSE7_REG),
};

static const struct regmap_access_table as3722_readable_table = {
	.yes_ranges = as3722_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(as3722_readable_ranges),
};

static const struct regmap_range as3722_writable_ranges[] = {
	regmap_reg_range(AS3722_SD0_VOLTAGE_REG, AS3722_SD6_VOLTAGE_REG),
	regmap_reg_range(AS3722_GPIO0_CONTROL_REG, AS3722_LDO7_VOLTAGE_REG),
	regmap_reg_range(AS3722_LDO9_VOLTAGE_REG, AS3722_GPIO_SIGNAL_OUT_REG),
	regmap_reg_range(AS3722_REG_SEQU_MOD1_REG, AS3722_REG_SEQU_MOD3_REG),
	regmap_reg_range(AS3722_SD_PHSW_CTRL_REG, AS3722_PWM_CONTROL_H_REG),
	regmap_reg_range(AS3722_WATCHDOG_TIMER_REG, AS3722_WATCHDOG_TIMER_REG),
	regmap_reg_range(AS3722_WATCHDOG_SOFTWARE_SIGNAL_REG,
					AS3722_BATTERY_VOLTAGE_MONITOR2_REG),
	regmap_reg_range(AS3722_SD_CONTROL_REG, AS3722_PWM_VCONTROL4_REG),
	regmap_reg_range(AS3722_BB_CHARGER_REG, AS3722_SRAM_REG),
	regmap_reg_range(AS3722_INTERRUPT_MASK1_REG, AS3722_TEMP_STATUS_REG),
	regmap_reg_range(AS3722_ADC0_CONTROL_REG, AS3722_ADC1_CONTROL_REG),
	regmap_reg_range(AS3722_ADC1_THRESHOLD_HI_MSB_REG,
					AS3722_ADC_CONFIGURATION_REG),
	regmap_reg_range(AS3722_LOCK_REG, AS3722_LOCK_REG),
};

static const struct regmap_access_table as3722_writable_table = {
	.yes_ranges = as3722_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(as3722_writable_ranges),
};

static const struct regmap_range as3722_cacheable_ranges[] = {
	regmap_reg_range(AS3722_SD0_VOLTAGE_REG, AS3722_LDO11_VOLTAGE_REG),
	regmap_reg_range(AS3722_SD_CONTROL_REG, AS3722_LDOCONTROL1_REG),
};

static const struct regmap_access_table as3722_volatile_table = {
	.no_ranges = as3722_cacheable_ranges,
	.n_no_ranges = ARRAY_SIZE(as3722_cacheable_ranges),
};

static const struct regmap_config as3722_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AS3722_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
	.rd_table = &as3722_readable_table,
	.wr_table = &as3722_writable_table,
	.volatile_table = &as3722_volatile_table,
};

static int as3722_i2c_of_probe(struct i2c_client *i2c,
			struct as3722 *as3722)
{
	struct device_node *np = i2c->dev.of_node;
	struct irq_data *irq_data;

	if (!np) {
		dev_err(&i2c->dev, "Device Tree not found\n");
		return -EINVAL;
	}

	irq_data = irq_get_irq_data(i2c->irq);
	if (!irq_data) {
		dev_err(&i2c->dev, "Invalid IRQ: %d\n", i2c->irq);
		return -EINVAL;
	}

	as3722->en_intern_int_pullup = of_property_read_bool(np,
					"ams,enable-internal-int-pullup");
	as3722->en_intern_i2c_pullup = of_property_read_bool(np,
					"ams,enable-internal-i2c-pullup");
	as3722->en_ac_ok_pwr_on = of_property_read_bool(np,
					"ams,enable-ac-ok-power-on");
	as3722->irq_flags = irqd_get_trigger_type(irq_data);
	dev_dbg(&i2c->dev, "IRQ flags are 0x%08lx\n", as3722->irq_flags);
	return 0;
}

static int as3722_i2c_probe(struct i2c_client *i2c,
			const struct i2c_device_id *id)
{
	struct as3722 *as3722;
	unsigned long irq_flags;
	int ret;
	u8 val = 0;

	as3722 = devm_kzalloc(&i2c->dev, sizeof(struct as3722), GFP_KERNEL);
	if (!as3722)
		return -ENOMEM;

	as3722->dev = &i2c->dev;
	as3722->chip_irq = i2c->irq;
	i2c_set_clientdata(i2c, as3722);

	ret = as3722_i2c_of_probe(i2c, as3722);
	if (ret < 0)
		return ret;

	as3722->regmap = devm_regmap_init_i2c(i2c, &as3722_regmap_config);
	if (IS_ERR(as3722->regmap)) {
		ret = PTR_ERR(as3722->regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	ret = as3722_check_device_id(as3722);
	if (ret < 0)
		return ret;

	irq_flags = as3722->irq_flags | IRQF_ONESHOT;
	ret = devm_regmap_add_irq_chip(as3722->dev, as3722->regmap,
				       as3722->chip_irq,
				       irq_flags, -1, &as3722_irq_chip,
				       &as3722->irq_data);
	if (ret < 0) {
		dev_err(as3722->dev, "Failed to add regmap irq: %d\n", ret);
		return ret;
	}

	ret = as3722_configure_pullups(as3722);
	if (ret < 0)
		return ret;

	if (as3722->en_ac_ok_pwr_on)
		val = AS3722_CTRL_SEQU1_AC_OK_PWR_ON;
	ret = as3722_update_bits(as3722, AS3722_CTRL_SEQU1_REG,
			AS3722_CTRL_SEQU1_AC_OK_PWR_ON, val);
	if (ret < 0) {
		dev_err(as3722->dev, "CTRLsequ1 update failed: %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(&i2c->dev, -1, as3722_devs,
				   ARRAY_SIZE(as3722_devs), NULL, 0,
				   regmap_irq_get_domain(as3722->irq_data));
	if (ret) {
		dev_err(as3722->dev, "Failed to add MFD devices: %d\n", ret);
		return ret;
	}

	device_init_wakeup(as3722->dev, true);

	dev_dbg(as3722->dev, "AS3722 core driver initialized successfully\n");
	return 0;
}

static int __maybe_unused as3722_i2c_suspend(struct device *dev)
{
	struct as3722 *as3722 = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(as3722->chip_irq);
	disable_irq(as3722->chip_irq);

	return 0;
}

static int __maybe_unused as3722_i2c_resume(struct device *dev)
{
	struct as3722 *as3722 = dev_get_drvdata(dev);

	enable_irq(as3722->chip_irq);

	if (device_may_wakeup(dev))
		disable_irq_wake(as3722->chip_irq);

	return 0;
}

static const struct of_device_id as3722_of_match[] = {
	{ .compatible = "ams,as3722", },
	{},
};
MODULE_DEVICE_TABLE(of, as3722_of_match);

static const struct i2c_device_id as3722_i2c_id[] = {
	{ "as3722", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, as3722_i2c_id);

static const struct dev_pm_ops as3722_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(as3722_i2c_suspend, as3722_i2c_resume)
};

static struct i2c_driver as3722_i2c_driver = {
	.driver = {
		.name = "as3722",
		.of_match_table = as3722_of_match,
		.pm = &as3722_pm_ops,
	},
	.probe = as3722_i2c_probe,
	.id_table = as3722_i2c_id,
};

module_i2c_driver(as3722_i2c_driver);

MODULE_DESCRIPTION("I2C support for AS3722 PMICs");
MODULE_AUTHOR("Florian Lobmaier <florian.lobmaier@ams.com>");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL");
