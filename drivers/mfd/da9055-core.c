// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device access for Dialog DA9055 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/mutex.h>

#include <linux/mfd/core.h>
#include <linux/mfd/da9055/core.h>
#include <linux/mfd/da9055/pdata.h>
#include <linux/mfd/da9055/reg.h>

#define DA9055_IRQ_NONKEY_MASK		0x01
#define DA9055_IRQ_ALM_MASK		0x02
#define DA9055_IRQ_TICK_MASK		0x04
#define DA9055_IRQ_ADC_MASK		0x08
#define DA9055_IRQ_BUCK_ILIM_MASK	0x08

static bool da9055_register_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA9055_REG_STATUS_A:
	case DA9055_REG_STATUS_B:
	case DA9055_REG_EVENT_A:
	case DA9055_REG_EVENT_B:
	case DA9055_REG_EVENT_C:
	case DA9055_REG_IRQ_MASK_A:
	case DA9055_REG_IRQ_MASK_B:
	case DA9055_REG_IRQ_MASK_C:

	case DA9055_REG_CONTROL_A:
	case DA9055_REG_CONTROL_B:
	case DA9055_REG_CONTROL_C:
	case DA9055_REG_CONTROL_D:
	case DA9055_REG_CONTROL_E:

	case DA9055_REG_ADC_MAN:
	case DA9055_REG_ADC_CONT:
	case DA9055_REG_VSYS_MON:
	case DA9055_REG_ADC_RES_L:
	case DA9055_REG_ADC_RES_H:
	case DA9055_REG_VSYS_RES:
	case DA9055_REG_ADCIN1_RES:
	case DA9055_REG_ADCIN2_RES:
	case DA9055_REG_ADCIN3_RES:

	case DA9055_REG_COUNT_S:
	case DA9055_REG_COUNT_MI:
	case DA9055_REG_COUNT_H:
	case DA9055_REG_COUNT_D:
	case DA9055_REG_COUNT_MO:
	case DA9055_REG_COUNT_Y:
	case DA9055_REG_ALARM_H:
	case DA9055_REG_ALARM_D:
	case DA9055_REG_ALARM_MI:
	case DA9055_REG_ALARM_MO:
	case DA9055_REG_ALARM_Y:

	case DA9055_REG_GPIO0_1:
	case DA9055_REG_GPIO2:
	case DA9055_REG_GPIO_MODE0_2:

	case DA9055_REG_BCORE_CONT:
	case DA9055_REG_BMEM_CONT:
	case DA9055_REG_LDO1_CONT:
	case DA9055_REG_LDO2_CONT:
	case DA9055_REG_LDO3_CONT:
	case DA9055_REG_LDO4_CONT:
	case DA9055_REG_LDO5_CONT:
	case DA9055_REG_LDO6_CONT:
	case DA9055_REG_BUCK_LIM:
	case DA9055_REG_BCORE_MODE:
	case DA9055_REG_VBCORE_A:
	case DA9055_REG_VBMEM_A:
	case DA9055_REG_VLDO1_A:
	case DA9055_REG_VLDO2_A:
	case DA9055_REG_VLDO3_A:
	case DA9055_REG_VLDO4_A:
	case DA9055_REG_VLDO5_A:
	case DA9055_REG_VLDO6_A:
	case DA9055_REG_VBCORE_B:
	case DA9055_REG_VBMEM_B:
	case DA9055_REG_VLDO1_B:
	case DA9055_REG_VLDO2_B:
	case DA9055_REG_VLDO3_B:
	case DA9055_REG_VLDO4_B:
	case DA9055_REG_VLDO5_B:
	case DA9055_REG_VLDO6_B:
		return true;
	default:
		return false;
	}
}

static bool da9055_register_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA9055_REG_STATUS_A:
	case DA9055_REG_STATUS_B:
	case DA9055_REG_EVENT_A:
	case DA9055_REG_EVENT_B:
	case DA9055_REG_EVENT_C:
	case DA9055_REG_IRQ_MASK_A:
	case DA9055_REG_IRQ_MASK_B:
	case DA9055_REG_IRQ_MASK_C:

	case DA9055_REG_CONTROL_A:
	case DA9055_REG_CONTROL_B:
	case DA9055_REG_CONTROL_C:
	case DA9055_REG_CONTROL_D:
	case DA9055_REG_CONTROL_E:

	case DA9055_REG_ADC_MAN:
	case DA9055_REG_ADC_CONT:
	case DA9055_REG_VSYS_MON:
	case DA9055_REG_ADC_RES_L:
	case DA9055_REG_ADC_RES_H:
	case DA9055_REG_VSYS_RES:
	case DA9055_REG_ADCIN1_RES:
	case DA9055_REG_ADCIN2_RES:
	case DA9055_REG_ADCIN3_RES:

	case DA9055_REG_COUNT_S:
	case DA9055_REG_COUNT_MI:
	case DA9055_REG_COUNT_H:
	case DA9055_REG_COUNT_D:
	case DA9055_REG_COUNT_MO:
	case DA9055_REG_COUNT_Y:
	case DA9055_REG_ALARM_H:
	case DA9055_REG_ALARM_D:
	case DA9055_REG_ALARM_MI:
	case DA9055_REG_ALARM_MO:
	case DA9055_REG_ALARM_Y:

	case DA9055_REG_GPIO0_1:
	case DA9055_REG_GPIO2:
	case DA9055_REG_GPIO_MODE0_2:

	case DA9055_REG_BCORE_CONT:
	case DA9055_REG_BMEM_CONT:
	case DA9055_REG_LDO1_CONT:
	case DA9055_REG_LDO2_CONT:
	case DA9055_REG_LDO3_CONT:
	case DA9055_REG_LDO4_CONT:
	case DA9055_REG_LDO5_CONT:
	case DA9055_REG_LDO6_CONT:
	case DA9055_REG_BUCK_LIM:
	case DA9055_REG_BCORE_MODE:
	case DA9055_REG_VBCORE_A:
	case DA9055_REG_VBMEM_A:
	case DA9055_REG_VLDO1_A:
	case DA9055_REG_VLDO2_A:
	case DA9055_REG_VLDO3_A:
	case DA9055_REG_VLDO4_A:
	case DA9055_REG_VLDO5_A:
	case DA9055_REG_VLDO6_A:
	case DA9055_REG_VBCORE_B:
	case DA9055_REG_VBMEM_B:
	case DA9055_REG_VLDO1_B:
	case DA9055_REG_VLDO2_B:
	case DA9055_REG_VLDO3_B:
	case DA9055_REG_VLDO4_B:
	case DA9055_REG_VLDO5_B:
	case DA9055_REG_VLDO6_B:
		return true;
	default:
		return false;
	}
}

static bool da9055_register_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA9055_REG_STATUS_A:
	case DA9055_REG_STATUS_B:
	case DA9055_REG_EVENT_A:
	case DA9055_REG_EVENT_B:
	case DA9055_REG_EVENT_C:

	case DA9055_REG_CONTROL_A:
	case DA9055_REG_CONTROL_E:

	case DA9055_REG_ADC_MAN:
	case DA9055_REG_ADC_RES_L:
	case DA9055_REG_ADC_RES_H:
	case DA9055_REG_VSYS_RES:
	case DA9055_REG_ADCIN1_RES:
	case DA9055_REG_ADCIN2_RES:
	case DA9055_REG_ADCIN3_RES:

	case DA9055_REG_COUNT_S:
	case DA9055_REG_COUNT_MI:
	case DA9055_REG_COUNT_H:
	case DA9055_REG_COUNT_D:
	case DA9055_REG_COUNT_MO:
	case DA9055_REG_COUNT_Y:
	case DA9055_REG_ALARM_MI:

	case DA9055_REG_BCORE_CONT:
	case DA9055_REG_BMEM_CONT:
	case DA9055_REG_LDO1_CONT:
	case DA9055_REG_LDO2_CONT:
	case DA9055_REG_LDO3_CONT:
	case DA9055_REG_LDO4_CONT:
	case DA9055_REG_LDO5_CONT:
	case DA9055_REG_LDO6_CONT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_irq da9055_irqs[] = {
	[DA9055_IRQ_NONKEY] = {
		.reg_offset = 0,
		.mask = DA9055_IRQ_NONKEY_MASK,
	},
	[DA9055_IRQ_ALARM] = {
		.reg_offset = 0,
		.mask = DA9055_IRQ_ALM_MASK,
	},
	[DA9055_IRQ_TICK] = {
		.reg_offset = 0,
		.mask = DA9055_IRQ_TICK_MASK,
	},
	[DA9055_IRQ_HWMON] = {
		.reg_offset = 0,
		.mask = DA9055_IRQ_ADC_MASK,
	},
	[DA9055_IRQ_REGULATOR] = {
		.reg_offset = 1,
		.mask = DA9055_IRQ_BUCK_ILIM_MASK,
	},
};

const struct regmap_config da9055_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_RBTREE,

	.max_register = DA9055_MAX_REGISTER_CNT,
	.readable_reg = da9055_register_readable,
	.writeable_reg = da9055_register_writeable,
	.volatile_reg = da9055_register_volatile,
};
EXPORT_SYMBOL_GPL(da9055_regmap_config);

static const struct resource da9055_onkey_resource =
	DEFINE_RES_IRQ_NAMED(DA9055_IRQ_NONKEY, "ONKEY");

static const struct resource da9055_rtc_resource[] = {
	DEFINE_RES_IRQ_NAMED(DA9055_IRQ_ALARM, "ALM"),
	DEFINE_RES_IRQ_NAMED(DA9055_IRQ_TICK, "TICK"),
};

static const struct resource da9055_hwmon_resource =
	DEFINE_RES_IRQ_NAMED(DA9055_IRQ_HWMON, "HWMON");

static const struct resource da9055_ld05_6_resource =
	DEFINE_RES_IRQ_NAMED(DA9055_IRQ_REGULATOR, "REGULATOR");

static const struct mfd_cell da9055_devs[] = {
	{
		.of_compatible = "dlg,da9055-gpio",
		.name = "da9055-gpio",
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.id = 1,
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.id = 2,
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.id = 3,
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.id = 4,
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.id = 5,
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.id = 6,
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.id = 7,
		.resources = &da9055_ld05_6_resource,
		.num_resources = 1,
	},
	{
		.of_compatible = "dlg,da9055-regulator",
		.name = "da9055-regulator",
		.resources = &da9055_ld05_6_resource,
		.num_resources = 1,
		.id = 8,
	},
	{
		.of_compatible = "dlg,da9055-onkey",
		.name = "da9055-onkey",
		.resources = &da9055_onkey_resource,
		.num_resources = 1,
	},
	{
		.of_compatible = "dlg,da9055-rtc",
		.name = "da9055-rtc",
		.resources = da9055_rtc_resource,
		.num_resources = ARRAY_SIZE(da9055_rtc_resource),
	},
	{
		.of_compatible = "dlg,da9055-hwmon",
		.name = "da9055-hwmon",
		.resources = &da9055_hwmon_resource,
		.num_resources = 1,
	},
	{
		.of_compatible = "dlg,da9055-watchdog",
		.name = "da9055-watchdog",
	},
};

static const struct regmap_irq_chip da9055_regmap_irq_chip = {
	.name = "da9055_irq",
	.status_base = DA9055_REG_EVENT_A,
	.mask_base = DA9055_REG_IRQ_MASK_A,
	.ack_base = DA9055_REG_EVENT_A,
	.num_regs = 3,
	.irqs = da9055_irqs,
	.num_irqs = ARRAY_SIZE(da9055_irqs),
};

int da9055_device_init(struct da9055 *da9055)
{
	struct da9055_pdata *pdata = dev_get_platdata(da9055->dev);
	int ret;
	uint8_t clear_events[3] = {0xFF, 0xFF, 0xFF};

	if (pdata && pdata->init != NULL)
		pdata->init(da9055);

	if (!pdata || !pdata->irq_base)
		da9055->irq_base = -1;
	else
		da9055->irq_base = pdata->irq_base;

	ret = da9055_group_write(da9055, DA9055_REG_EVENT_A, 3, clear_events);
	if (ret < 0)
		return ret;

	ret = regmap_add_irq_chip(da9055->regmap, da9055->chip_irq,
				  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				  da9055->irq_base, &da9055_regmap_irq_chip,
				  &da9055->irq_data);
	if (ret < 0)
		return ret;

	da9055->irq_base = regmap_irq_chip_get_base(da9055->irq_data);

	ret = mfd_add_devices(da9055->dev, -1,
			      da9055_devs, ARRAY_SIZE(da9055_devs),
			      NULL, da9055->irq_base, NULL);
	if (ret)
		goto err;

	return 0;

err:
	mfd_remove_devices(da9055->dev);
	return ret;
}

void da9055_device_exit(struct da9055 *da9055)
{
	regmap_del_irq_chip(da9055->chip_irq, da9055->irq_data);
	mfd_remove_devices(da9055->dev);
}

MODULE_DESCRIPTION("Core support for the DA9055 PMIC");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
