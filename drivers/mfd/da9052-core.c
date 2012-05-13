/*
 * Device access for Dialog DA9052 PMICs.
 *
 * Copyright(c) 2011 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/pdata.h>
#include <linux/mfd/da9052/reg.h>

#define DA9052_NUM_IRQ_REGS		4
#define DA9052_IRQ_MASK_POS_1		0x01
#define DA9052_IRQ_MASK_POS_2		0x02
#define DA9052_IRQ_MASK_POS_3		0x04
#define DA9052_IRQ_MASK_POS_4		0x08
#define DA9052_IRQ_MASK_POS_5		0x10
#define DA9052_IRQ_MASK_POS_6		0x20
#define DA9052_IRQ_MASK_POS_7		0x40
#define DA9052_IRQ_MASK_POS_8		0x80

static bool da9052_reg_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA9052_PAGE0_CON_REG:
	case DA9052_STATUS_A_REG:
	case DA9052_STATUS_B_REG:
	case DA9052_STATUS_C_REG:
	case DA9052_STATUS_D_REG:
	case DA9052_EVENT_A_REG:
	case DA9052_EVENT_B_REG:
	case DA9052_EVENT_C_REG:
	case DA9052_EVENT_D_REG:
	case DA9052_FAULTLOG_REG:
	case DA9052_IRQ_MASK_A_REG:
	case DA9052_IRQ_MASK_B_REG:
	case DA9052_IRQ_MASK_C_REG:
	case DA9052_IRQ_MASK_D_REG:
	case DA9052_CONTROL_A_REG:
	case DA9052_CONTROL_B_REG:
	case DA9052_CONTROL_C_REG:
	case DA9052_CONTROL_D_REG:
	case DA9052_PDDIS_REG:
	case DA9052_INTERFACE_REG:
	case DA9052_RESET_REG:
	case DA9052_GPIO_0_1_REG:
	case DA9052_GPIO_2_3_REG:
	case DA9052_GPIO_4_5_REG:
	case DA9052_GPIO_6_7_REG:
	case DA9052_GPIO_14_15_REG:
	case DA9052_ID_0_1_REG:
	case DA9052_ID_2_3_REG:
	case DA9052_ID_4_5_REG:
	case DA9052_ID_6_7_REG:
	case DA9052_ID_8_9_REG:
	case DA9052_ID_10_11_REG:
	case DA9052_ID_12_13_REG:
	case DA9052_ID_14_15_REG:
	case DA9052_ID_16_17_REG:
	case DA9052_ID_18_19_REG:
	case DA9052_ID_20_21_REG:
	case DA9052_SEQ_STATUS_REG:
	case DA9052_SEQ_A_REG:
	case DA9052_SEQ_B_REG:
	case DA9052_SEQ_TIMER_REG:
	case DA9052_BUCKA_REG:
	case DA9052_BUCKB_REG:
	case DA9052_BUCKCORE_REG:
	case DA9052_BUCKPRO_REG:
	case DA9052_BUCKMEM_REG:
	case DA9052_BUCKPERI_REG:
	case DA9052_LDO1_REG:
	case DA9052_LDO2_REG:
	case DA9052_LDO3_REG:
	case DA9052_LDO4_REG:
	case DA9052_LDO5_REG:
	case DA9052_LDO6_REG:
	case DA9052_LDO7_REG:
	case DA9052_LDO8_REG:
	case DA9052_LDO9_REG:
	case DA9052_LDO10_REG:
	case DA9052_SUPPLY_REG:
	case DA9052_PULLDOWN_REG:
	case DA9052_CHGBUCK_REG:
	case DA9052_WAITCONT_REG:
	case DA9052_ISET_REG:
	case DA9052_BATCHG_REG:
	case DA9052_CHG_CONT_REG:
	case DA9052_INPUT_CONT_REG:
	case DA9052_CHG_TIME_REG:
	case DA9052_BBAT_CONT_REG:
	case DA9052_BOOST_REG:
	case DA9052_LED_CONT_REG:
	case DA9052_LEDMIN123_REG:
	case DA9052_LED1_CONF_REG:
	case DA9052_LED2_CONF_REG:
	case DA9052_LED3_CONF_REG:
	case DA9052_LED1CONT_REG:
	case DA9052_LED2CONT_REG:
	case DA9052_LED3CONT_REG:
	case DA9052_LED_CONT_4_REG:
	case DA9052_LED_CONT_5_REG:
	case DA9052_ADC_MAN_REG:
	case DA9052_ADC_CONT_REG:
	case DA9052_ADC_RES_L_REG:
	case DA9052_ADC_RES_H_REG:
	case DA9052_VDD_RES_REG:
	case DA9052_VDD_MON_REG:
	case DA9052_ICHG_AV_REG:
	case DA9052_ICHG_THD_REG:
	case DA9052_ICHG_END_REG:
	case DA9052_TBAT_RES_REG:
	case DA9052_TBAT_HIGHP_REG:
	case DA9052_TBAT_HIGHN_REG:
	case DA9052_TBAT_LOW_REG:
	case DA9052_T_OFFSET_REG:
	case DA9052_ADCIN4_RES_REG:
	case DA9052_AUTO4_HIGH_REG:
	case DA9052_AUTO4_LOW_REG:
	case DA9052_ADCIN5_RES_REG:
	case DA9052_AUTO5_HIGH_REG:
	case DA9052_AUTO5_LOW_REG:
	case DA9052_ADCIN6_RES_REG:
	case DA9052_AUTO6_HIGH_REG:
	case DA9052_AUTO6_LOW_REG:
	case DA9052_TJUNC_RES_REG:
	case DA9052_TSI_CONT_A_REG:
	case DA9052_TSI_CONT_B_REG:
	case DA9052_TSI_X_MSB_REG:
	case DA9052_TSI_Y_MSB_REG:
	case DA9052_TSI_LSB_REG:
	case DA9052_TSI_Z_MSB_REG:
	case DA9052_COUNT_S_REG:
	case DA9052_COUNT_MI_REG:
	case DA9052_COUNT_H_REG:
	case DA9052_COUNT_D_REG:
	case DA9052_COUNT_MO_REG:
	case DA9052_COUNT_Y_REG:
	case DA9052_ALARM_MI_REG:
	case DA9052_ALARM_H_REG:
	case DA9052_ALARM_D_REG:
	case DA9052_ALARM_MO_REG:
	case DA9052_ALARM_Y_REG:
	case DA9052_SECOND_A_REG:
	case DA9052_SECOND_B_REG:
	case DA9052_SECOND_C_REG:
	case DA9052_SECOND_D_REG:
	case DA9052_PAGE1_CON_REG:
		return true;
	default:
		return false;
	}
}

static bool da9052_reg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA9052_PAGE0_CON_REG:
	case DA9052_EVENT_A_REG:
	case DA9052_EVENT_B_REG:
	case DA9052_EVENT_C_REG:
	case DA9052_EVENT_D_REG:
	case DA9052_IRQ_MASK_A_REG:
	case DA9052_IRQ_MASK_B_REG:
	case DA9052_IRQ_MASK_C_REG:
	case DA9052_IRQ_MASK_D_REG:
	case DA9052_CONTROL_A_REG:
	case DA9052_CONTROL_B_REG:
	case DA9052_CONTROL_C_REG:
	case DA9052_CONTROL_D_REG:
	case DA9052_PDDIS_REG:
	case DA9052_RESET_REG:
	case DA9052_GPIO_0_1_REG:
	case DA9052_GPIO_2_3_REG:
	case DA9052_GPIO_4_5_REG:
	case DA9052_GPIO_6_7_REG:
	case DA9052_GPIO_14_15_REG:
	case DA9052_ID_0_1_REG:
	case DA9052_ID_2_3_REG:
	case DA9052_ID_4_5_REG:
	case DA9052_ID_6_7_REG:
	case DA9052_ID_8_9_REG:
	case DA9052_ID_10_11_REG:
	case DA9052_ID_12_13_REG:
	case DA9052_ID_14_15_REG:
	case DA9052_ID_16_17_REG:
	case DA9052_ID_18_19_REG:
	case DA9052_ID_20_21_REG:
	case DA9052_SEQ_STATUS_REG:
	case DA9052_SEQ_A_REG:
	case DA9052_SEQ_B_REG:
	case DA9052_SEQ_TIMER_REG:
	case DA9052_BUCKA_REG:
	case DA9052_BUCKB_REG:
	case DA9052_BUCKCORE_REG:
	case DA9052_BUCKPRO_REG:
	case DA9052_BUCKMEM_REG:
	case DA9052_BUCKPERI_REG:
	case DA9052_LDO1_REG:
	case DA9052_LDO2_REG:
	case DA9052_LDO3_REG:
	case DA9052_LDO4_REG:
	case DA9052_LDO5_REG:
	case DA9052_LDO6_REG:
	case DA9052_LDO7_REG:
	case DA9052_LDO8_REG:
	case DA9052_LDO9_REG:
	case DA9052_LDO10_REG:
	case DA9052_SUPPLY_REG:
	case DA9052_PULLDOWN_REG:
	case DA9052_CHGBUCK_REG:
	case DA9052_WAITCONT_REG:
	case DA9052_ISET_REG:
	case DA9052_BATCHG_REG:
	case DA9052_CHG_CONT_REG:
	case DA9052_INPUT_CONT_REG:
	case DA9052_BBAT_CONT_REG:
	case DA9052_BOOST_REG:
	case DA9052_LED_CONT_REG:
	case DA9052_LEDMIN123_REG:
	case DA9052_LED1_CONF_REG:
	case DA9052_LED2_CONF_REG:
	case DA9052_LED3_CONF_REG:
	case DA9052_LED1CONT_REG:
	case DA9052_LED2CONT_REG:
	case DA9052_LED3CONT_REG:
	case DA9052_LED_CONT_4_REG:
	case DA9052_LED_CONT_5_REG:
	case DA9052_ADC_MAN_REG:
	case DA9052_ADC_CONT_REG:
	case DA9052_ADC_RES_L_REG:
	case DA9052_ADC_RES_H_REG:
	case DA9052_VDD_RES_REG:
	case DA9052_VDD_MON_REG:
	case DA9052_ICHG_THD_REG:
	case DA9052_ICHG_END_REG:
	case DA9052_TBAT_HIGHP_REG:
	case DA9052_TBAT_HIGHN_REG:
	case DA9052_TBAT_LOW_REG:
	case DA9052_T_OFFSET_REG:
	case DA9052_AUTO4_HIGH_REG:
	case DA9052_AUTO4_LOW_REG:
	case DA9052_AUTO5_HIGH_REG:
	case DA9052_AUTO5_LOW_REG:
	case DA9052_AUTO6_HIGH_REG:
	case DA9052_AUTO6_LOW_REG:
	case DA9052_TSI_CONT_A_REG:
	case DA9052_TSI_CONT_B_REG:
	case DA9052_COUNT_S_REG:
	case DA9052_COUNT_MI_REG:
	case DA9052_COUNT_H_REG:
	case DA9052_COUNT_D_REG:
	case DA9052_COUNT_MO_REG:
	case DA9052_COUNT_Y_REG:
	case DA9052_ALARM_MI_REG:
	case DA9052_ALARM_H_REG:
	case DA9052_ALARM_D_REG:
	case DA9052_ALARM_MO_REG:
	case DA9052_ALARM_Y_REG:
	case DA9052_PAGE1_CON_REG:
		return true;
	default:
		return false;
	}
}

static bool da9052_reg_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA9052_STATUS_A_REG:
	case DA9052_STATUS_B_REG:
	case DA9052_STATUS_C_REG:
	case DA9052_STATUS_D_REG:
	case DA9052_EVENT_A_REG:
	case DA9052_EVENT_B_REG:
	case DA9052_EVENT_C_REG:
	case DA9052_EVENT_D_REG:
	case DA9052_FAULTLOG_REG:
	case DA9052_CHG_TIME_REG:
	case DA9052_ADC_RES_L_REG:
	case DA9052_ADC_RES_H_REG:
	case DA9052_VDD_RES_REG:
	case DA9052_ICHG_AV_REG:
	case DA9052_TBAT_RES_REG:
	case DA9052_ADCIN4_RES_REG:
	case DA9052_ADCIN5_RES_REG:
	case DA9052_ADCIN6_RES_REG:
	case DA9052_TJUNC_RES_REG:
	case DA9052_TSI_X_MSB_REG:
	case DA9052_TSI_Y_MSB_REG:
	case DA9052_TSI_LSB_REG:
	case DA9052_TSI_Z_MSB_REG:
	case DA9052_COUNT_S_REG:
	case DA9052_COUNT_MI_REG:
	case DA9052_COUNT_H_REG:
	case DA9052_COUNT_D_REG:
	case DA9052_COUNT_MO_REG:
	case DA9052_COUNT_Y_REG:
	case DA9052_ALARM_MI_REG:
		return true;
	default:
		return false;
	}
}

static struct resource da9052_rtc_resource = {
	.name = "ALM",
	.start = DA9052_IRQ_ALARM,
	.end   = DA9052_IRQ_ALARM,
	.flags = IORESOURCE_IRQ,
};

static struct resource da9052_onkey_resource = {
	.name = "ONKEY",
	.start = DA9052_IRQ_NONKEY,
	.end   = DA9052_IRQ_NONKEY,
	.flags = IORESOURCE_IRQ,
};

static struct resource da9052_bat_resources[] = {
	{
		.name = "BATT TEMP",
		.start = DA9052_IRQ_TBAT,
		.end   = DA9052_IRQ_TBAT,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "DCIN DET",
		.start = DA9052_IRQ_DCIN,
		.end   = DA9052_IRQ_DCIN,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "DCIN REM",
		.start = DA9052_IRQ_DCINREM,
		.end   = DA9052_IRQ_DCINREM,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS DET",
		.start = DA9052_IRQ_VBUS,
		.end   = DA9052_IRQ_VBUS,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "VBUS REM",
		.start = DA9052_IRQ_VBUSREM,
		.end   = DA9052_IRQ_VBUSREM,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "CHG END",
		.start = DA9052_IRQ_CHGEND,
		.end   = DA9052_IRQ_CHGEND,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource da9052_tsi_resources[] = {
	{
		.name = "PENDWN",
		.start = DA9052_IRQ_PENDOWN,
		.end   = DA9052_IRQ_PENDOWN,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "TSIRDY",
		.start = DA9052_IRQ_TSIREADY,
		.end   = DA9052_IRQ_TSIREADY,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell __devinitdata da9052_subdev_info[] = {
	{
		.name = "da9052-regulator",
		.id = 1,
	},
	{
		.name = "da9052-regulator",
		.id = 2,
	},
	{
		.name = "da9052-regulator",
		.id = 3,
	},
	{
		.name = "da9052-regulator",
		.id = 4,
	},
	{
		.name = "da9052-regulator",
		.id = 5,
	},
	{
		.name = "da9052-regulator",
		.id = 6,
	},
	{
		.name = "da9052-regulator",
		.id = 7,
	},
	{
		.name = "da9052-regulator",
		.id = 8,
	},
	{
		.name = "da9052-regulator",
		.id = 9,
	},
	{
		.name = "da9052-regulator",
		.id = 10,
	},
	{
		.name = "da9052-regulator",
		.id = 11,
	},
	{
		.name = "da9052-regulator",
		.id = 12,
	},
	{
		.name = "da9052-regulator",
		.id = 13,
	},
	{
		.name = "da9052-regulator",
		.id = 14,
	},
	{
		.name = "da9052-onkey",
		.resources = &da9052_onkey_resource,
		.num_resources = 1,
	},
	{
		.name = "da9052-rtc",
		.resources = &da9052_rtc_resource,
		.num_resources = 1,
	},
	{
		.name = "da9052-gpio",
	},
	{
		.name = "da9052-hwmon",
	},
	{
		.name = "da9052-leds",
	},
	{
		.name = "da9052-wled1",
	},
	{
		.name = "da9052-wled2",
	},
	{
		.name = "da9052-wled3",
	},
	{
		.name = "da9052-tsi",
		.resources = da9052_tsi_resources,
		.num_resources = ARRAY_SIZE(da9052_tsi_resources),
	},
	{
		.name = "da9052-bat",
		.resources = da9052_bat_resources,
		.num_resources = ARRAY_SIZE(da9052_bat_resources),
	},
	{
		.name = "da9052-watchdog",
	},
};

static struct regmap_irq da9052_irqs[] = {
	[DA9052_IRQ_DCIN] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_VBUS] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_DCINREM] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_VBUSREM] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_VDDLOW] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_ALARM] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_SEQRDY] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_COMP1V2] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
	[DA9052_IRQ_NONKEY] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_IDFLOAT] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_IDGND] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_CHGEND] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_TBAT] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_ADC_EOM] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_PENDOWN] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_TSIREADY] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
	[DA9052_IRQ_GPI0] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_GPI1] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_GPI2] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_GPI3] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_GPI4] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_GPI5] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_GPI6] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_GPI7] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
	[DA9052_IRQ_GPI8] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_GPI9] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_GPI10] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_GPI11] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_GPI12] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_GPI13] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_GPI14] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_GPI15] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
};

static struct regmap_irq_chip da9052_regmap_irq_chip = {
	.name = "da9052_irq",
	.status_base = DA9052_EVENT_A_REG,
	.mask_base = DA9052_IRQ_MASK_A_REG,
	.ack_base = DA9052_EVENT_A_REG,
	.num_regs = DA9052_NUM_IRQ_REGS,
	.irqs = da9052_irqs,
	.num_irqs = ARRAY_SIZE(da9052_irqs),
};

struct regmap_config da9052_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_RBTREE,

	.max_register = DA9052_PAGE1_CON_REG,
	.readable_reg = da9052_reg_readable,
	.writeable_reg = da9052_reg_writeable,
	.volatile_reg = da9052_reg_volatile,
};
EXPORT_SYMBOL_GPL(da9052_regmap_config);

int __devinit da9052_device_init(struct da9052 *da9052, u8 chip_id)
{
	struct da9052_pdata *pdata = da9052->dev->platform_data;
	struct irq_desc *desc;
	int ret;

	if (pdata && pdata->init != NULL)
		pdata->init(da9052);

	da9052->chip_id = chip_id;

	if (!pdata || !pdata->irq_base)
		da9052->irq_base = -1;
	else
		da9052->irq_base = pdata->irq_base;

	ret = regmap_add_irq_chip(da9052->regmap, da9052->chip_irq,
				  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				  da9052->irq_base, &da9052_regmap_irq_chip,
				  &da9052->irq_data);
	if (ret < 0)
		goto regmap_err;

	da9052->irq_base = regmap_irq_chip_get_base(da9052->irq_data);

	ret = mfd_add_devices(da9052->dev, -1, da9052_subdev_info,
			      ARRAY_SIZE(da9052_subdev_info), NULL, 0);
	if (ret)
		goto err;

	return 0;

err:
	mfd_remove_devices(da9052->dev);
regmap_err:
	return ret;
}

void da9052_device_exit(struct da9052 *da9052)
{
	regmap_del_irq_chip(da9052->chip_irq, da9052->irq_data);
	mfd_remove_devices(da9052->dev);
}

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("DA9052 MFD Core");
MODULE_LICENSE("GPL");
