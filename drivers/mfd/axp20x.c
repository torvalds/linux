/*
 * axp20x.c - MFD core driver for the X-Powers' Power Management ICs
 *
 * AXP20x typically comprises an adaptive USB-Compatible PWM charger, BUCK DC-DC
 * converters, LDOs, multiple 12-bit ADCs of voltage, current and temperature
 * as well as configurable GPIOs.
 *
 * Author: Carlo Caione <carlo@caione.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/axp20x.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/acpi.h>

#define AXP20X_OFF	0x80

static const char * const axp20x_model_names[] = {
	"AXP202",
	"AXP209",
	"AXP221",
	"AXP288",
};

static const struct regmap_range axp20x_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP20X_FG_RES),
};

static const struct regmap_range axp20x_volatile_ranges[] = {
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ5_STATE),
};

static const struct regmap_access_table axp20x_writeable_table = {
	.yes_ranges	= axp20x_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_writeable_ranges),
};

static const struct regmap_access_table axp20x_volatile_table = {
	.yes_ranges	= axp20x_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_volatile_ranges),
};

static const struct regmap_range axp22x_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP22X_BATLOW_THRES1),
};

static const struct regmap_range axp22x_volatile_ranges[] = {
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ5_STATE),
};

static const struct regmap_access_table axp22x_writeable_table = {
	.yes_ranges	= axp22x_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp22x_writeable_ranges),
};

static const struct regmap_access_table axp22x_volatile_table = {
	.yes_ranges	= axp22x_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp22x_volatile_ranges),
};

static const struct regmap_range axp288_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ6_STATE),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP288_FG_TUNE5),
};

static const struct regmap_range axp288_volatile_ranges[] = {
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IPSOUT_V_HIGH_L),
};

static const struct regmap_access_table axp288_writeable_table = {
	.yes_ranges	= axp288_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp288_writeable_ranges),
};

static const struct regmap_access_table axp288_volatile_table = {
	.yes_ranges	= axp288_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp288_volatile_ranges),
};

static struct resource axp20x_pek_resources[] = {
	{
		.name	= "PEK_DBR",
		.start	= AXP20X_IRQ_PEK_RIS_EDGE,
		.end	= AXP20X_IRQ_PEK_RIS_EDGE,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "PEK_DBF",
		.start	= AXP20X_IRQ_PEK_FAL_EDGE,
		.end	= AXP20X_IRQ_PEK_FAL_EDGE,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource axp22x_pek_resources[] = {
	{
		.name   = "PEK_DBR",
		.start  = AXP22X_IRQ_PEK_RIS_EDGE,
		.end    = AXP22X_IRQ_PEK_RIS_EDGE,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "PEK_DBF",
		.start  = AXP22X_IRQ_PEK_FAL_EDGE,
		.end    = AXP22X_IRQ_PEK_FAL_EDGE,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource axp288_fuel_gauge_resources[] = {
	{
		.start = AXP288_IRQ_QWBTU,
		.end   = AXP288_IRQ_QWBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WBTU,
		.end   = AXP288_IRQ_WBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_QWBTO,
		.end   = AXP288_IRQ_QWBTO,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WBTO,
		.end   = AXP288_IRQ_WBTO,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WL2,
		.end   = AXP288_IRQ_WL2,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WL1,
		.end   = AXP288_IRQ_WL1,
		.flags = IORESOURCE_IRQ,
	},
};

static const struct regmap_config axp20x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp20x_writeable_table,
	.volatile_table	= &axp20x_volatile_table,
	.max_register	= AXP20X_FG_RES,
	.cache_type	= REGCACHE_RBTREE,
};

static const struct regmap_config axp22x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp22x_writeable_table,
	.volatile_table	= &axp22x_volatile_table,
	.max_register	= AXP22X_BATLOW_THRES1,
	.cache_type	= REGCACHE_RBTREE,
};

static const struct regmap_config axp288_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp288_writeable_table,
	.volatile_table	= &axp288_volatile_table,
	.max_register	= AXP288_FG_TUNE5,
	.cache_type	= REGCACHE_RBTREE,
};

#define INIT_REGMAP_IRQ(_variant, _irq, _off, _mask)			\
	[_variant##_IRQ_##_irq] = { .reg_offset = (_off), .mask = BIT(_mask) }

static const struct regmap_irq axp20x_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP20X, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP20X, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP20X, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP20X, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP20X, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP20X, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP20X, VBUS_V_LOW,		0, 1),
	INIT_REGMAP_IRQ(AXP20X, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP20X, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP20X, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP20X, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP20X, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP20X, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP20X, BATT_TEMP_HIGH,	        1, 1),
	INIT_REGMAP_IRQ(AXP20X, BATT_TEMP_LOW,	        1, 0),
	INIT_REGMAP_IRQ(AXP20X, DIE_TEMP_HIGH,	        2, 7),
	INIT_REGMAP_IRQ(AXP20X, CHARG_I_LOW,		2, 6),
	INIT_REGMAP_IRQ(AXP20X, DCDC1_V_LONG,	        2, 5),
	INIT_REGMAP_IRQ(AXP20X, DCDC2_V_LONG,	        2, 4),
	INIT_REGMAP_IRQ(AXP20X, DCDC3_V_LONG,	        2, 3),
	INIT_REGMAP_IRQ(AXP20X, PEK_SHORT,		2, 1),
	INIT_REGMAP_IRQ(AXP20X, PEK_LONG,		2, 0),
	INIT_REGMAP_IRQ(AXP20X, N_OE_PWR_ON,		3, 7),
	INIT_REGMAP_IRQ(AXP20X, N_OE_PWR_OFF,	        3, 6),
	INIT_REGMAP_IRQ(AXP20X, VBUS_VALID,		3, 5),
	INIT_REGMAP_IRQ(AXP20X, VBUS_NOT_VALID,	        3, 4),
	INIT_REGMAP_IRQ(AXP20X, VBUS_SESS_VALID,	3, 3),
	INIT_REGMAP_IRQ(AXP20X, VBUS_SESS_END,	        3, 2),
	INIT_REGMAP_IRQ(AXP20X, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP20X, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP20X, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP20X, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP20X, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP20X, GPIO3_INPUT,		4, 3),
	INIT_REGMAP_IRQ(AXP20X, GPIO2_INPUT,		4, 2),
	INIT_REGMAP_IRQ(AXP20X, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP20X, GPIO0_INPUT,		4, 0),
};

static const struct regmap_irq axp22x_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP22X, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP22X, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP22X, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP22X, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP22X, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP22X, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP22X, VBUS_V_LOW,		0, 1),
	INIT_REGMAP_IRQ(AXP22X, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP22X, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP22X, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP22X, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP22X, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP22X, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP22X, BATT_TEMP_HIGH,	        1, 1),
	INIT_REGMAP_IRQ(AXP22X, BATT_TEMP_LOW,	        1, 0),
	INIT_REGMAP_IRQ(AXP22X, DIE_TEMP_HIGH,	        2, 7),
	INIT_REGMAP_IRQ(AXP22X, PEK_SHORT,		2, 1),
	INIT_REGMAP_IRQ(AXP22X, PEK_LONG,		2, 0),
	INIT_REGMAP_IRQ(AXP22X, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP22X, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP22X, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP22X, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP22X, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP22X, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP22X, GPIO0_INPUT,		4, 0),
};

/* some IRQs are compatible with axp20x models */
static const struct regmap_irq axp288_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP288, VBUS_FALL,              0, 2),
	INIT_REGMAP_IRQ(AXP288, VBUS_RISE,              0, 3),
	INIT_REGMAP_IRQ(AXP288, OV,                     0, 4),

	INIT_REGMAP_IRQ(AXP288, DONE,                   1, 2),
	INIT_REGMAP_IRQ(AXP288, CHARGING,               1, 3),
	INIT_REGMAP_IRQ(AXP288, SAFE_QUIT,              1, 4),
	INIT_REGMAP_IRQ(AXP288, SAFE_ENTER,             1, 5),
	INIT_REGMAP_IRQ(AXP288, ABSENT,                 1, 6),
	INIT_REGMAP_IRQ(AXP288, APPEND,                 1, 7),

	INIT_REGMAP_IRQ(AXP288, QWBTU,                  2, 0),
	INIT_REGMAP_IRQ(AXP288, WBTU,                   2, 1),
	INIT_REGMAP_IRQ(AXP288, QWBTO,                  2, 2),
	INIT_REGMAP_IRQ(AXP288, WBTO,                   2, 3),
	INIT_REGMAP_IRQ(AXP288, QCBTU,                  2, 4),
	INIT_REGMAP_IRQ(AXP288, CBTU,                   2, 5),
	INIT_REGMAP_IRQ(AXP288, QCBTO,                  2, 6),
	INIT_REGMAP_IRQ(AXP288, CBTO,                   2, 7),

	INIT_REGMAP_IRQ(AXP288, WL2,                    3, 0),
	INIT_REGMAP_IRQ(AXP288, WL1,                    3, 1),
	INIT_REGMAP_IRQ(AXP288, GPADC,                  3, 2),
	INIT_REGMAP_IRQ(AXP288, OT,                     3, 7),

	INIT_REGMAP_IRQ(AXP288, GPIO0,                  4, 0),
	INIT_REGMAP_IRQ(AXP288, GPIO1,                  4, 1),
	INIT_REGMAP_IRQ(AXP288, POKO,                   4, 2),
	INIT_REGMAP_IRQ(AXP288, POKL,                   4, 3),
	INIT_REGMAP_IRQ(AXP288, POKS,                   4, 4),
	INIT_REGMAP_IRQ(AXP288, POKN,                   4, 5),
	INIT_REGMAP_IRQ(AXP288, POKP,                   4, 6),
	INIT_REGMAP_IRQ(AXP288, TIMER,                  4, 7),

	INIT_REGMAP_IRQ(AXP288, MV_CHNG,                5, 0),
	INIT_REGMAP_IRQ(AXP288, BC_USB_CHNG,            5, 1),
};

static const struct of_device_id axp20x_of_match[] = {
	{ .compatible = "x-powers,axp202", .data = (void *) AXP202_ID },
	{ .compatible = "x-powers,axp209", .data = (void *) AXP209_ID },
	{ .compatible = "x-powers,axp221", .data = (void *) AXP221_ID },
	{ },
};
MODULE_DEVICE_TABLE(of, axp20x_of_match);

/*
 * This is useless for OF-enabled devices, but it is needed by I2C subsystem
 */
static const struct i2c_device_id axp20x_i2c_id[] = {
	{ },
};
MODULE_DEVICE_TABLE(i2c, axp20x_i2c_id);

static const struct acpi_device_id axp20x_acpi_match[] = {
	{
		.id = "INT33F4",
		.driver_data = AXP288_ID,
	},
	{ },
};
MODULE_DEVICE_TABLE(acpi, axp20x_acpi_match);

static const struct regmap_irq_chip axp20x_regmap_irq_chip = {
	.name			= "axp20x_irq_chip",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp20x_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp20x_regmap_irqs),
	.num_regs		= 5,

};

static const struct regmap_irq_chip axp22x_regmap_irq_chip = {
	.name			= "axp22x_irq_chip",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp22x_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp22x_regmap_irqs),
	.num_regs		= 5,
};

static const struct regmap_irq_chip axp288_regmap_irq_chip = {
	.name			= "axp288_irq_chip",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp288_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp288_regmap_irqs),
	.num_regs		= 6,

};

static struct mfd_cell axp20x_cells[] = {
	{
		.name			= "axp20x-pek",
		.num_resources		= ARRAY_SIZE(axp20x_pek_resources),
		.resources		= axp20x_pek_resources,
	}, {
		.name			= "axp20x-regulator",
	},
};

static struct mfd_cell axp22x_cells[] = {
	{
		.name			= "axp20x-pek",
		.num_resources		= ARRAY_SIZE(axp22x_pek_resources),
		.resources		= axp22x_pek_resources,
	}, {
		.name			= "axp20x-regulator",
	},
};

static struct resource axp288_adc_resources[] = {
	{
		.name  = "GPADC",
		.start = AXP288_IRQ_GPADC,
		.end   = AXP288_IRQ_GPADC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource axp288_extcon_resources[] = {
	{
		.start = AXP288_IRQ_VBUS_FALL,
		.end   = AXP288_IRQ_VBUS_FALL,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_VBUS_RISE,
		.end   = AXP288_IRQ_VBUS_RISE,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_MV_CHNG,
		.end   = AXP288_IRQ_MV_CHNG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_BC_USB_CHNG,
		.end   = AXP288_IRQ_BC_USB_CHNG,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource axp288_charger_resources[] = {
	{
		.start = AXP288_IRQ_OV,
		.end   = AXP288_IRQ_OV,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_DONE,
		.end   = AXP288_IRQ_DONE,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_CHARGING,
		.end   = AXP288_IRQ_CHARGING,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_SAFE_QUIT,
		.end   = AXP288_IRQ_SAFE_QUIT,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_SAFE_ENTER,
		.end   = AXP288_IRQ_SAFE_ENTER,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_QCBTU,
		.end   = AXP288_IRQ_QCBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_CBTU,
		.end   = AXP288_IRQ_CBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_QCBTO,
		.end   = AXP288_IRQ_QCBTO,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_CBTO,
		.end   = AXP288_IRQ_CBTO,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell axp288_cells[] = {
	{
		.name = "axp288_adc",
		.num_resources = ARRAY_SIZE(axp288_adc_resources),
		.resources = axp288_adc_resources,
	},
	{
		.name = "axp288_extcon",
		.num_resources = ARRAY_SIZE(axp288_extcon_resources),
		.resources = axp288_extcon_resources,
	},
	{
		.name = "axp288_charger",
		.num_resources = ARRAY_SIZE(axp288_charger_resources),
		.resources = axp288_charger_resources,
	},
	{
		.name = "axp288_fuel_gauge",
		.num_resources = ARRAY_SIZE(axp288_fuel_gauge_resources),
		.resources = axp288_fuel_gauge_resources,
	},
	{
		.name = "axp288_pmic_acpi",
	},
};

static struct axp20x_dev *axp20x_pm_power_off;
static void axp20x_power_off(void)
{
	if (axp20x_pm_power_off->variant == AXP288_ID)
		return;

	regmap_write(axp20x_pm_power_off->regmap, AXP20X_OFF_CTRL,
		     AXP20X_OFF);
}

static int axp20x_match_device(struct axp20x_dev *axp20x, struct device *dev)
{
	const struct acpi_device_id *acpi_id;
	const struct of_device_id *of_id;

	if (dev->of_node) {
		of_id = of_match_device(axp20x_of_match, dev);
		if (!of_id) {
			dev_err(dev, "Unable to match OF ID\n");
			return -ENODEV;
		}
		axp20x->variant = (long) of_id->data;
	} else {
		acpi_id = acpi_match_device(dev->driver->acpi_match_table, dev);
		if (!acpi_id || !acpi_id->driver_data) {
			dev_err(dev, "Unable to match ACPI ID and data\n");
			return -ENODEV;
		}
		axp20x->variant = (long) acpi_id->driver_data;
	}

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp20x_cells);
		axp20x->cells = axp20x_cells;
		axp20x->regmap_cfg = &axp20x_regmap_config;
		axp20x->regmap_irq_chip = &axp20x_regmap_irq_chip;
		break;
	case AXP221_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp22x_cells);
		axp20x->cells = axp22x_cells;
		axp20x->regmap_cfg = &axp22x_regmap_config;
		axp20x->regmap_irq_chip = &axp22x_regmap_irq_chip;
		break;
	case AXP288_ID:
		axp20x->cells = axp288_cells;
		axp20x->nr_cells = ARRAY_SIZE(axp288_cells);
		axp20x->regmap_cfg = &axp288_regmap_config;
		axp20x->regmap_irq_chip = &axp288_regmap_irq_chip;
		break;
	default:
		dev_err(dev, "unsupported AXP20X ID %lu\n", axp20x->variant);
		return -EINVAL;
	}
	dev_info(dev, "AXP20x variant %s found\n",
		axp20x_model_names[axp20x->variant]);

	return 0;
}

static int axp20x_i2c_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct axp20x_dev *axp20x;
	int ret;

	axp20x = devm_kzalloc(&i2c->dev, sizeof(*axp20x), GFP_KERNEL);
	if (!axp20x)
		return -ENOMEM;

	ret = axp20x_match_device(axp20x, &i2c->dev);
	if (ret)
		return ret;

	axp20x->i2c_client = i2c;
	axp20x->dev = &i2c->dev;
	dev_set_drvdata(axp20x->dev, axp20x);

	axp20x->regmap = devm_regmap_init_i2c(i2c, axp20x->regmap_cfg);
	if (IS_ERR(axp20x->regmap)) {
		ret = PTR_ERR(axp20x->regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	ret = regmap_add_irq_chip(axp20x->regmap, i2c->irq,
				  IRQF_ONESHOT | IRQF_SHARED, -1,
				  axp20x->regmap_irq_chip,
				  &axp20x->regmap_irqc);
	if (ret) {
		dev_err(&i2c->dev, "failed to add irq chip: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(axp20x->dev, -1, axp20x->cells,
			axp20x->nr_cells, NULL, 0, NULL);

	if (ret) {
		dev_err(&i2c->dev, "failed to add MFD devices: %d\n", ret);
		regmap_del_irq_chip(i2c->irq, axp20x->regmap_irqc);
		return ret;
	}

	if (!pm_power_off) {
		axp20x_pm_power_off = axp20x;
		pm_power_off = axp20x_power_off;
	}

	dev_info(&i2c->dev, "AXP20X driver loaded\n");

	return 0;
}

static int axp20x_i2c_remove(struct i2c_client *i2c)
{
	struct axp20x_dev *axp20x = i2c_get_clientdata(i2c);

	if (axp20x == axp20x_pm_power_off) {
		axp20x_pm_power_off = NULL;
		pm_power_off = NULL;
	}

	mfd_remove_devices(axp20x->dev);
	regmap_del_irq_chip(axp20x->i2c_client->irq, axp20x->regmap_irqc);

	return 0;
}

static struct i2c_driver axp20x_i2c_driver = {
	.driver = {
		.name	= "axp20x",
		.of_match_table	= of_match_ptr(axp20x_of_match),
		.acpi_match_table = ACPI_PTR(axp20x_acpi_match),
	},
	.probe		= axp20x_i2c_probe,
	.remove		= axp20x_i2c_remove,
	.id_table	= axp20x_i2c_id,
};

module_i2c_driver(axp20x_i2c_driver);

MODULE_DESCRIPTION("PMIC MFD core driver for AXP20X");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_LICENSE("GPL");
