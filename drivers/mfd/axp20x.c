/*
 * MFD core driver for the X-Powers' Power Management ICs
 *
 * AXP20x typically comprises an adaptive USB-Compatible PWM charger, BUCK DC-DC
 * converters, LDOs, multiple 12-bit ADCs of voltage, current and temperature
 * as well as configurable GPIOs.
 *
 * This file contains the interface independent core functions.
 *
 * Copyright (C) 2014 Carlo Caione
 *
 * Author: Carlo Caione <carlo@caione.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/axp20x.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>
#include <linux/acpi.h>

#define AXP20X_OFF	0x80

#define AXP806_REG_ADDR_EXT_ADDR_MASTER_MODE	0
#define AXP806_REG_ADDR_EXT_ADDR_SLAVE_MODE	BIT(4)

static const char * const axp20x_model_names[] = {
	"AXP152",
	"AXP202",
	"AXP209",
	"AXP221",
	"AXP223",
	"AXP288",
	"AXP803",
	"AXP806",
	"AXP809",
	"AXP813",
};

static const struct regmap_range axp152_writeable_ranges[] = {
	regmap_reg_range(AXP152_LDO3456_DC1234_CTRL, AXP152_IRQ3_STATE),
	regmap_reg_range(AXP152_DCDC_MODE, AXP152_PWM1_DUTY_CYCLE),
};

static const struct regmap_range axp152_volatile_ranges[] = {
	regmap_reg_range(AXP152_PWR_OP_MODE, AXP152_PWR_OP_MODE),
	regmap_reg_range(AXP152_IRQ1_EN, AXP152_IRQ3_STATE),
	regmap_reg_range(AXP152_GPIO_INPUT, AXP152_GPIO_INPUT),
};

static const struct regmap_access_table axp152_writeable_table = {
	.yes_ranges	= axp152_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp152_writeable_ranges),
};

static const struct regmap_access_table axp152_volatile_table = {
	.yes_ranges	= axp152_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp152_volatile_ranges),
};

static const struct regmap_range axp20x_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_CHRG_CTRL1, AXP20X_CHRG_CTRL2),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP20X_FG_RES),
	regmap_reg_range(AXP20X_RDC_H, AXP20X_OCV(AXP20X_OCV_MAX)),
};

static const struct regmap_range axp20x_volatile_ranges[] = {
	regmap_reg_range(AXP20X_PWR_INPUT_STATUS, AXP20X_USB_OTG_STATUS),
	regmap_reg_range(AXP20X_CHRG_CTRL1, AXP20X_CHRG_CTRL2),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_ACIN_V_ADC_H, AXP20X_IPSOUT_V_HIGH_L),
	regmap_reg_range(AXP20X_GPIO20_SS, AXP20X_GPIO3_CTRL),
	regmap_reg_range(AXP20X_FG_RES, AXP20X_RDC_L),
};

static const struct regmap_access_table axp20x_writeable_table = {
	.yes_ranges	= axp20x_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_writeable_ranges),
};

static const struct regmap_access_table axp20x_volatile_table = {
	.yes_ranges	= axp20x_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_volatile_ranges),
};

/* AXP22x ranges are shared with the AXP809, as they cover the same range */
static const struct regmap_range axp22x_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_CHRG_CTRL1, AXP22X_CHRG_CTRL3),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP22X_BATLOW_THRES1),
};

static const struct regmap_range axp22x_volatile_ranges[] = {
	regmap_reg_range(AXP20X_PWR_INPUT_STATUS, AXP20X_PWR_OP_MODE),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP22X_GPIO_STATE, AXP22X_GPIO_STATE),
	regmap_reg_range(AXP22X_PMIC_TEMP_H, AXP20X_IPSOUT_V_HIGH_L),
	regmap_reg_range(AXP20X_FG_RES, AXP20X_FG_RES),
};

static const struct regmap_access_table axp22x_writeable_table = {
	.yes_ranges	= axp22x_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp22x_writeable_ranges),
};

static const struct regmap_access_table axp22x_volatile_table = {
	.yes_ranges	= axp22x_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp22x_volatile_ranges),
};

/* AXP288 ranges are shared with the AXP803, as they cover the same range */
static const struct regmap_range axp288_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ6_STATE),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP288_FG_TUNE5),
};

static const struct regmap_range axp288_volatile_ranges[] = {
	regmap_reg_range(AXP20X_PWR_INPUT_STATUS, AXP288_POWER_REASON),
	regmap_reg_range(AXP288_BC_GLOBAL, AXP288_BC_GLOBAL),
	regmap_reg_range(AXP288_BC_DET_STAT, AXP288_BC_DET_STAT),
	regmap_reg_range(AXP20X_CHRG_BAK_CTRL, AXP20X_CHRG_BAK_CTRL),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IPSOUT_V_HIGH_L),
	regmap_reg_range(AXP20X_TIMER_CTRL, AXP20X_TIMER_CTRL),
	regmap_reg_range(AXP22X_GPIO_STATE, AXP22X_GPIO_STATE),
	regmap_reg_range(AXP288_RT_BATT_V_H, AXP288_RT_BATT_V_L),
	regmap_reg_range(AXP20X_FG_RES, AXP288_FG_CC_CAP_REG),
};

static const struct regmap_access_table axp288_writeable_table = {
	.yes_ranges	= axp288_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp288_writeable_ranges),
};

static const struct regmap_access_table axp288_volatile_table = {
	.yes_ranges	= axp288_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp288_volatile_ranges),
};

static const struct regmap_range axp806_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_DATACACHE(3)),
	regmap_reg_range(AXP806_PWR_OUT_CTRL1, AXP806_CLDO3_V_CTRL),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ2_EN),
	regmap_reg_range(AXP20X_IRQ1_STATE, AXP20X_IRQ2_STATE),
	regmap_reg_range(AXP806_REG_ADDR_EXT, AXP806_REG_ADDR_EXT),
};

static const struct regmap_range axp806_volatile_ranges[] = {
	regmap_reg_range(AXP20X_IRQ1_STATE, AXP20X_IRQ2_STATE),
};

static const struct regmap_access_table axp806_writeable_table = {
	.yes_ranges	= axp806_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp806_writeable_ranges),
};

static const struct regmap_access_table axp806_volatile_table = {
	.yes_ranges	= axp806_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp806_volatile_ranges),
};

static const struct resource axp152_pek_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP152_IRQ_PEK_RIS_EDGE, "PEK_DBR"),
	DEFINE_RES_IRQ_NAMED(AXP152_IRQ_PEK_FAL_EDGE, "PEK_DBF"),
};

static const struct resource axp20x_ac_power_supply_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_ACIN_PLUGIN, "ACIN_PLUGIN"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_ACIN_REMOVAL, "ACIN_REMOVAL"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_ACIN_OVER_V, "ACIN_OVER_V"),
};

static const struct resource axp20x_pek_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_PEK_RIS_EDGE, "PEK_DBR"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_PEK_FAL_EDGE, "PEK_DBF"),
};

static const struct resource axp20x_usb_power_supply_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_PLUGIN, "VBUS_PLUGIN"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_REMOVAL, "VBUS_REMOVAL"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_VALID, "VBUS_VALID"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_NOT_VALID, "VBUS_NOT_VALID"),
};

static const struct resource axp22x_usb_power_supply_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP22X_IRQ_VBUS_PLUGIN, "VBUS_PLUGIN"),
	DEFINE_RES_IRQ_NAMED(AXP22X_IRQ_VBUS_REMOVAL, "VBUS_REMOVAL"),
};

static const struct resource axp22x_pek_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP22X_IRQ_PEK_RIS_EDGE, "PEK_DBR"),
	DEFINE_RES_IRQ_NAMED(AXP22X_IRQ_PEK_FAL_EDGE, "PEK_DBF"),
};

static const struct resource axp288_power_button_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP288_IRQ_POKP, "PEK_DBR"),
	DEFINE_RES_IRQ_NAMED(AXP288_IRQ_POKN, "PEK_DBF"),
};

static const struct resource axp288_fuel_gauge_resources[] = {
	DEFINE_RES_IRQ(AXP288_IRQ_QWBTU),
	DEFINE_RES_IRQ(AXP288_IRQ_WBTU),
	DEFINE_RES_IRQ(AXP288_IRQ_QWBTO),
	DEFINE_RES_IRQ(AXP288_IRQ_WBTO),
	DEFINE_RES_IRQ(AXP288_IRQ_WL2),
	DEFINE_RES_IRQ(AXP288_IRQ_WL1),
};

static const struct resource axp803_pek_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP803_IRQ_PEK_RIS_EDGE, "PEK_DBR"),
	DEFINE_RES_IRQ_NAMED(AXP803_IRQ_PEK_FAL_EDGE, "PEK_DBF"),
};

static const struct resource axp809_pek_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP809_IRQ_PEK_RIS_EDGE, "PEK_DBR"),
	DEFINE_RES_IRQ_NAMED(AXP809_IRQ_PEK_FAL_EDGE, "PEK_DBF"),
};

static const struct regmap_config axp152_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp152_writeable_table,
	.volatile_table	= &axp152_volatile_table,
	.max_register	= AXP152_PWM1_DUTY_CYCLE,
	.cache_type	= REGCACHE_RBTREE,
};

static const struct regmap_config axp20x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp20x_writeable_table,
	.volatile_table	= &axp20x_volatile_table,
	.max_register	= AXP20X_OCV(AXP20X_OCV_MAX),
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

static const struct regmap_config axp806_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp806_writeable_table,
	.volatile_table	= &axp806_volatile_table,
	.max_register	= AXP806_REG_ADDR_EXT,
	.cache_type	= REGCACHE_RBTREE,
};

#define INIT_REGMAP_IRQ(_variant, _irq, _off, _mask)			\
	[_variant##_IRQ_##_irq] = { .reg_offset = (_off), .mask = BIT(_mask) }

static const struct regmap_irq axp152_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP152, LDO0IN_CONNECT,		0, 6),
	INIT_REGMAP_IRQ(AXP152, LDO0IN_REMOVAL,		0, 5),
	INIT_REGMAP_IRQ(AXP152, ALDO0IN_CONNECT,	0, 3),
	INIT_REGMAP_IRQ(AXP152, ALDO0IN_REMOVAL,	0, 2),
	INIT_REGMAP_IRQ(AXP152, DCDC1_V_LOW,		1, 5),
	INIT_REGMAP_IRQ(AXP152, DCDC2_V_LOW,		1, 4),
	INIT_REGMAP_IRQ(AXP152, DCDC3_V_LOW,		1, 3),
	INIT_REGMAP_IRQ(AXP152, DCDC4_V_LOW,		1, 2),
	INIT_REGMAP_IRQ(AXP152, PEK_SHORT,		1, 1),
	INIT_REGMAP_IRQ(AXP152, PEK_LONG,		1, 0),
	INIT_REGMAP_IRQ(AXP152, TIMER,			2, 7),
	INIT_REGMAP_IRQ(AXP152, PEK_RIS_EDGE,		2, 6),
	INIT_REGMAP_IRQ(AXP152, PEK_FAL_EDGE,		2, 5),
	INIT_REGMAP_IRQ(AXP152, GPIO3_INPUT,		2, 3),
	INIT_REGMAP_IRQ(AXP152, GPIO2_INPUT,		2, 2),
	INIT_REGMAP_IRQ(AXP152, GPIO1_INPUT,		2, 1),
	INIT_REGMAP_IRQ(AXP152, GPIO0_INPUT,		2, 0),
};

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
	INIT_REGMAP_IRQ(AXP288, FALLING_ALT,            0, 5),
	INIT_REGMAP_IRQ(AXP288, RISING_ALT,             0, 6),
	INIT_REGMAP_IRQ(AXP288, OV_ALT,                 0, 7),

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

static const struct regmap_irq axp803_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP803, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP803, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP803, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP803, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP803, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP803, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP803, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP803, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP803, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP803, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP803, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP803, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_HIGH,	2, 7),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_HIGH_END,	2, 6),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_LOW,	2, 5),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_LOW_END,	2, 4),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_HIGH,	2, 3),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_HIGH_END,	2, 2),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_LOW,	2, 1),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_LOW_END,	2, 0),
	INIT_REGMAP_IRQ(AXP803, DIE_TEMP_HIGH,	        3, 7),
	INIT_REGMAP_IRQ(AXP803, GPADC,		        3, 2),
	INIT_REGMAP_IRQ(AXP803, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP803, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP803, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP803, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP803, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP803, PEK_SHORT,		4, 4),
	INIT_REGMAP_IRQ(AXP803, PEK_LONG,		4, 3),
	INIT_REGMAP_IRQ(AXP803, PEK_OVER_OFF,		4, 2),
	INIT_REGMAP_IRQ(AXP803, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP803, GPIO0_INPUT,		4, 0),
	INIT_REGMAP_IRQ(AXP803, BC_USB_CHNG,            5, 1),
	INIT_REGMAP_IRQ(AXP803, MV_CHNG,                5, 0),
};

static const struct regmap_irq axp806_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP806, DIE_TEMP_HIGH_LV1,	0, 0),
	INIT_REGMAP_IRQ(AXP806, DIE_TEMP_HIGH_LV2,	0, 1),
	INIT_REGMAP_IRQ(AXP806, DCDCA_V_LOW,		0, 3),
	INIT_REGMAP_IRQ(AXP806, DCDCB_V_LOW,		0, 4),
	INIT_REGMAP_IRQ(AXP806, DCDCC_V_LOW,		0, 5),
	INIT_REGMAP_IRQ(AXP806, DCDCD_V_LOW,		0, 6),
	INIT_REGMAP_IRQ(AXP806, DCDCE_V_LOW,		0, 7),
	INIT_REGMAP_IRQ(AXP806, POK_LONG,		1, 0),
	INIT_REGMAP_IRQ(AXP806, POK_SHORT,		1, 1),
	INIT_REGMAP_IRQ(AXP806, WAKEUP,			1, 4),
	INIT_REGMAP_IRQ(AXP806, POK_FALL,		1, 5),
	INIT_REGMAP_IRQ(AXP806, POK_RISE,		1, 6),
};

static const struct regmap_irq axp809_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP809, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP809, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP809, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP809, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP809, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP809, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP809, VBUS_V_LOW,		0, 1),
	INIT_REGMAP_IRQ(AXP809, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP809, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP809, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP809, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP809, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP809, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_HIGH,	2, 7),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_HIGH_END,	2, 6),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_LOW,	2, 5),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_LOW_END,	2, 4),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_HIGH,	2, 3),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_HIGH_END,	2, 2),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_LOW,	2, 1),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_LOW_END,	2, 0),
	INIT_REGMAP_IRQ(AXP809, DIE_TEMP_HIGH,	        3, 7),
	INIT_REGMAP_IRQ(AXP809, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP809, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP809, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP809, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP809, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP809, PEK_SHORT,		4, 4),
	INIT_REGMAP_IRQ(AXP809, PEK_LONG,		4, 3),
	INIT_REGMAP_IRQ(AXP809, PEK_OVER_OFF,		4, 2),
	INIT_REGMAP_IRQ(AXP809, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP809, GPIO0_INPUT,		4, 0),
};

static const struct regmap_irq_chip axp152_regmap_irq_chip = {
	.name			= "axp152_irq_chip",
	.status_base		= AXP152_IRQ1_STATE,
	.ack_base		= AXP152_IRQ1_STATE,
	.mask_base		= AXP152_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp152_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp152_regmap_irqs),
	.num_regs		= 3,
};

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

static const struct regmap_irq_chip axp803_regmap_irq_chip = {
	.name			= "axp803",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp803_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp803_regmap_irqs),
	.num_regs		= 6,
};

static const struct regmap_irq_chip axp806_regmap_irq_chip = {
	.name			= "axp806",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp806_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp806_regmap_irqs),
	.num_regs		= 2,
};

static const struct regmap_irq_chip axp809_regmap_irq_chip = {
	.name			= "axp809",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp809_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp809_regmap_irqs),
	.num_regs		= 5,
};

static const struct mfd_cell axp20x_cells[] = {
	{
		.name		= "axp20x-gpio",
		.of_compatible	= "x-powers,axp209-gpio",
	}, {
		.name		= "axp20x-pek",
		.num_resources	= ARRAY_SIZE(axp20x_pek_resources),
		.resources	= axp20x_pek_resources,
	}, {
		.name		= "axp20x-regulator",
	}, {
		.name		= "axp20x-adc",
		.of_compatible	= "x-powers,axp209-adc",
	}, {
		.name		= "axp20x-battery-power-supply",
		.of_compatible	= "x-powers,axp209-battery-power-supply",
	}, {
		.name		= "axp20x-ac-power-supply",
		.of_compatible	= "x-powers,axp202-ac-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_ac_power_supply_resources),
		.resources	= axp20x_ac_power_supply_resources,
	}, {
		.name		= "axp20x-usb-power-supply",
		.of_compatible	= "x-powers,axp202-usb-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_usb_power_supply_resources),
		.resources	= axp20x_usb_power_supply_resources,
	},
};

static const struct mfd_cell axp221_cells[] = {
	{
		.name		= "axp221-pek",
		.num_resources	= ARRAY_SIZE(axp22x_pek_resources),
		.resources	= axp22x_pek_resources,
	}, {
		.name		= "axp20x-regulator",
	}, {
		.name		= "axp22x-adc",
		.of_compatible	= "x-powers,axp221-adc",
	}, {
		.name		= "axp20x-ac-power-supply",
		.of_compatible	= "x-powers,axp221-ac-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_ac_power_supply_resources),
		.resources	= axp20x_ac_power_supply_resources,
	}, {
		.name		= "axp20x-battery-power-supply",
		.of_compatible	= "x-powers,axp221-battery-power-supply",
	}, {
		.name		= "axp20x-usb-power-supply",
		.of_compatible	= "x-powers,axp221-usb-power-supply",
		.num_resources	= ARRAY_SIZE(axp22x_usb_power_supply_resources),
		.resources	= axp22x_usb_power_supply_resources,
	},
};

static const struct mfd_cell axp223_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp22x_pek_resources),
		.resources		= axp22x_pek_resources,
	}, {
		.name		= "axp22x-adc",
		.of_compatible	= "x-powers,axp221-adc",
	}, {
		.name		= "axp20x-battery-power-supply",
		.of_compatible	= "x-powers,axp221-battery-power-supply",
	}, {
		.name			= "axp20x-regulator",
	}, {
		.name		= "axp20x-ac-power-supply",
		.of_compatible	= "x-powers,axp221-ac-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_ac_power_supply_resources),
		.resources	= axp20x_ac_power_supply_resources,
	}, {
		.name		= "axp20x-usb-power-supply",
		.of_compatible	= "x-powers,axp223-usb-power-supply",
		.num_resources	= ARRAY_SIZE(axp22x_usb_power_supply_resources),
		.resources	= axp22x_usb_power_supply_resources,
	},
};

static const struct mfd_cell axp152_cells[] = {
	{
		.name			= "axp20x-pek",
		.num_resources		= ARRAY_SIZE(axp152_pek_resources),
		.resources		= axp152_pek_resources,
	},
};

static const struct resource axp288_adc_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP288_IRQ_GPADC, "GPADC"),
};

static const struct resource axp288_extcon_resources[] = {
	DEFINE_RES_IRQ(AXP288_IRQ_VBUS_FALL),
	DEFINE_RES_IRQ(AXP288_IRQ_VBUS_RISE),
	DEFINE_RES_IRQ(AXP288_IRQ_MV_CHNG),
	DEFINE_RES_IRQ(AXP288_IRQ_BC_USB_CHNG),
};

static const struct resource axp288_charger_resources[] = {
	DEFINE_RES_IRQ(AXP288_IRQ_OV),
	DEFINE_RES_IRQ(AXP288_IRQ_DONE),
	DEFINE_RES_IRQ(AXP288_IRQ_CHARGING),
	DEFINE_RES_IRQ(AXP288_IRQ_SAFE_QUIT),
	DEFINE_RES_IRQ(AXP288_IRQ_SAFE_ENTER),
	DEFINE_RES_IRQ(AXP288_IRQ_QCBTU),
	DEFINE_RES_IRQ(AXP288_IRQ_CBTU),
	DEFINE_RES_IRQ(AXP288_IRQ_QCBTO),
	DEFINE_RES_IRQ(AXP288_IRQ_CBTO),
};

static const struct mfd_cell axp288_cells[] = {
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
		.name = "axp221-pek",
		.num_resources = ARRAY_SIZE(axp288_power_button_resources),
		.resources = axp288_power_button_resources,
	},
	{
		.name = "axp288_pmic_acpi",
	},
};

static const struct mfd_cell axp803_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp803_pek_resources),
		.resources		= axp803_pek_resources,
	},
	{	.name			= "axp20x-regulator" },
};

static const struct mfd_cell axp806_cells[] = {
	{
		.id			= 2,
		.name			= "axp20x-regulator",
	},
};

static const struct mfd_cell axp809_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp809_pek_resources),
		.resources		= axp809_pek_resources,
	}, {
		.id			= 1,
		.name			= "axp20x-regulator",
	},
};

static const struct mfd_cell axp813_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp803_pek_resources),
		.resources		= axp803_pek_resources,
	}, {
		.name			= "axp20x-regulator",
	}, {
		.name			= "axp20x-gpio",
		.of_compatible		= "x-powers,axp813-gpio",
	}, {
		.name			= "axp813-adc",
		.of_compatible		= "x-powers,axp813-adc",
	}, {
		.name		= "axp20x-battery-power-supply",
		.of_compatible	= "x-powers,axp813-battery-power-supply",
	},
};

static struct axp20x_dev *axp20x_pm_power_off;
static void axp20x_power_off(void)
{
	if (axp20x_pm_power_off->variant == AXP288_ID)
		return;

	regmap_write(axp20x_pm_power_off->regmap, AXP20X_OFF_CTRL,
		     AXP20X_OFF);

	/* Give capacitors etc. time to drain to avoid kernel panic msg. */
	msleep(500);
}

int axp20x_match_device(struct axp20x_dev *axp20x)
{
	struct device *dev = axp20x->dev;
	const struct acpi_device_id *acpi_id;
	const struct of_device_id *of_id;

	if (dev->of_node) {
		of_id = of_match_device(dev->driver->of_match_table, dev);
		if (!of_id) {
			dev_err(dev, "Unable to match OF ID\n");
			return -ENODEV;
		}
		axp20x->variant = (long)of_id->data;
	} else {
		acpi_id = acpi_match_device(dev->driver->acpi_match_table, dev);
		if (!acpi_id || !acpi_id->driver_data) {
			dev_err(dev, "Unable to match ACPI ID and data\n");
			return -ENODEV;
		}
		axp20x->variant = (long)acpi_id->driver_data;
	}

	switch (axp20x->variant) {
	case AXP152_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp152_cells);
		axp20x->cells = axp152_cells;
		axp20x->regmap_cfg = &axp152_regmap_config;
		axp20x->regmap_irq_chip = &axp152_regmap_irq_chip;
		break;
	case AXP202_ID:
	case AXP209_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp20x_cells);
		axp20x->cells = axp20x_cells;
		axp20x->regmap_cfg = &axp20x_regmap_config;
		axp20x->regmap_irq_chip = &axp20x_regmap_irq_chip;
		break;
	case AXP221_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp221_cells);
		axp20x->cells = axp221_cells;
		axp20x->regmap_cfg = &axp22x_regmap_config;
		axp20x->regmap_irq_chip = &axp22x_regmap_irq_chip;
		break;
	case AXP223_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp223_cells);
		axp20x->cells = axp223_cells;
		axp20x->regmap_cfg = &axp22x_regmap_config;
		axp20x->regmap_irq_chip = &axp22x_regmap_irq_chip;
		break;
	case AXP288_ID:
		axp20x->cells = axp288_cells;
		axp20x->nr_cells = ARRAY_SIZE(axp288_cells);
		axp20x->regmap_cfg = &axp288_regmap_config;
		axp20x->regmap_irq_chip = &axp288_regmap_irq_chip;
		axp20x->irq_flags = IRQF_TRIGGER_LOW;
		break;
	case AXP803_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp803_cells);
		axp20x->cells = axp803_cells;
		axp20x->regmap_cfg = &axp288_regmap_config;
		axp20x->regmap_irq_chip = &axp803_regmap_irq_chip;
		break;
	case AXP806_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp806_cells);
		axp20x->cells = axp806_cells;
		axp20x->regmap_cfg = &axp806_regmap_config;
		axp20x->regmap_irq_chip = &axp806_regmap_irq_chip;
		break;
	case AXP809_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp809_cells);
		axp20x->cells = axp809_cells;
		axp20x->regmap_cfg = &axp22x_regmap_config;
		axp20x->regmap_irq_chip = &axp809_regmap_irq_chip;
		break;
	case AXP813_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp813_cells);
		axp20x->cells = axp813_cells;
		axp20x->regmap_cfg = &axp288_regmap_config;
		/*
		 * The IRQ table given in the datasheet is incorrect.
		 * In IRQ enable/status registers 1, there are separate
		 * IRQs for ACIN and VBUS, instead of bits [7:5] being
		 * the same as bits [4:2]. So it shares the same IRQs
		 * as the AXP803, rather than the AXP288.
		 */
		axp20x->regmap_irq_chip = &axp803_regmap_irq_chip;
		break;
	default:
		dev_err(dev, "unsupported AXP20X ID %lu\n", axp20x->variant);
		return -EINVAL;
	}
	dev_info(dev, "AXP20x variant %s found\n",
		 axp20x_model_names[axp20x->variant]);

	return 0;
}
EXPORT_SYMBOL(axp20x_match_device);

int axp20x_device_probe(struct axp20x_dev *axp20x)
{
	int ret;

	/*
	 * The AXP806 supports either master/standalone or slave mode.
	 * Slave mode allows sharing the serial bus, even with multiple
	 * AXP806 which all have the same hardware address.
	 *
	 * This is done with extra "serial interface address extension",
	 * or AXP806_BUS_ADDR_EXT, and "register address extension", or
	 * AXP806_REG_ADDR_EXT, registers. The former is read-only, with
	 * 1 bit customizable at the factory, and 1 bit depending on the
	 * state of an external pin. The latter is writable. The device
	 * will only respond to operations to its other registers when
	 * the these device addressing bits (in the upper 4 bits of the
	 * registers) match.
	 *
	 * By default we support an AXP806 chained to an AXP809 in slave
	 * mode. Boards which use an AXP806 in master mode can set the
	 * property "x-powers,master-mode" to override the default.
	 */
	if (axp20x->variant == AXP806_ID) {
		if (of_property_read_bool(axp20x->dev->of_node,
					  "x-powers,master-mode"))
			regmap_write(axp20x->regmap, AXP806_REG_ADDR_EXT,
				     AXP806_REG_ADDR_EXT_ADDR_MASTER_MODE);
		else
			regmap_write(axp20x->regmap, AXP806_REG_ADDR_EXT,
				     AXP806_REG_ADDR_EXT_ADDR_SLAVE_MODE);
	}

	ret = regmap_add_irq_chip(axp20x->regmap, axp20x->irq,
			  IRQF_ONESHOT | IRQF_SHARED | axp20x->irq_flags,
			   -1, axp20x->regmap_irq_chip, &axp20x->regmap_irqc);
	if (ret) {
		dev_err(axp20x->dev, "failed to add irq chip: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(axp20x->dev, -1, axp20x->cells,
			      axp20x->nr_cells, NULL, 0, NULL);

	if (ret) {
		dev_err(axp20x->dev, "failed to add MFD devices: %d\n", ret);
		regmap_del_irq_chip(axp20x->irq, axp20x->regmap_irqc);
		return ret;
	}

	if (!pm_power_off) {
		axp20x_pm_power_off = axp20x;
		pm_power_off = axp20x_power_off;
	}

	dev_info(axp20x->dev, "AXP20X driver loaded\n");

	return 0;
}
EXPORT_SYMBOL(axp20x_device_probe);

int axp20x_device_remove(struct axp20x_dev *axp20x)
{
	if (axp20x == axp20x_pm_power_off) {
		axp20x_pm_power_off = NULL;
		pm_power_off = NULL;
	}

	mfd_remove_devices(axp20x->dev);
	regmap_del_irq_chip(axp20x->irq, axp20x->regmap_irqc);

	return 0;
}
EXPORT_SYMBOL(axp20x_device_remove);

MODULE_DESCRIPTION("PMIC MFD core driver for AXP20X");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_LICENSE("GPL");
