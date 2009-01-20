/* NXP PCF50633 GPIO Driver
 *
 * (C) 2006-2008 by Openmoko, Inc.
 * Author: Balaji Rao <balajirrao@openmoko.org>
 * All rights reserved.
 *
 * Broken down from monstrous PCF50633 driver mainly by
 * Harald Welte, Andy Green and Werner Almesberger
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>

#include <linux/mfd/pcf50633/core.h>
#include <linux/mfd/pcf50633/gpio.h>

enum pcf50633_regulator_id {
	PCF50633_REGULATOR_AUTO,
	PCF50633_REGULATOR_DOWN1,
	PCF50633_REGULATOR_DOWN2,
	PCF50633_REGULATOR_LDO1,
	PCF50633_REGULATOR_LDO2,
	PCF50633_REGULATOR_LDO3,
	PCF50633_REGULATOR_LDO4,
	PCF50633_REGULATOR_LDO5,
	PCF50633_REGULATOR_LDO6,
	PCF50633_REGULATOR_HCLDO,
	PCF50633_REGULATOR_MEMLDO,
};

#define PCF50633_REG_AUTOOUT	0x1a
#define PCF50633_REG_DOWN1OUT	0x1e
#define PCF50633_REG_DOWN2OUT	0x22
#define PCF50633_REG_MEMLDOOUT	0x26
#define PCF50633_REG_LDO1OUT	0x2d
#define PCF50633_REG_LDO2OUT	0x2f
#define PCF50633_REG_LDO3OUT	0x31
#define PCF50633_REG_LDO4OUT	0x33
#define PCF50633_REG_LDO5OUT	0x35
#define PCF50633_REG_LDO6OUT	0x37
#define PCF50633_REG_HCLDOOUT	0x39

static const u8 pcf50633_regulator_registers[PCF50633_NUM_REGULATORS] = {
	[PCF50633_REGULATOR_AUTO]	= PCF50633_REG_AUTOOUT,
	[PCF50633_REGULATOR_DOWN1]	= PCF50633_REG_DOWN1OUT,
	[PCF50633_REGULATOR_DOWN2]	= PCF50633_REG_DOWN2OUT,
	[PCF50633_REGULATOR_MEMLDO]	= PCF50633_REG_MEMLDOOUT,
	[PCF50633_REGULATOR_LDO1]	= PCF50633_REG_LDO1OUT,
	[PCF50633_REGULATOR_LDO2]	= PCF50633_REG_LDO2OUT,
	[PCF50633_REGULATOR_LDO3]	= PCF50633_REG_LDO3OUT,
	[PCF50633_REGULATOR_LDO4]	= PCF50633_REG_LDO4OUT,
	[PCF50633_REGULATOR_LDO5]	= PCF50633_REG_LDO5OUT,
	[PCF50633_REGULATOR_LDO6]	= PCF50633_REG_LDO6OUT,
	[PCF50633_REGULATOR_HCLDO]	= PCF50633_REG_HCLDOOUT,
};

int pcf50633_gpio_set(struct pcf50633 *pcf, int gpio, u8 val)
{
	u8 reg;

	reg = gpio - PCF50633_GPIO1 + PCF50633_REG_GPIO1CFG;

	return pcf50633_reg_set_bit_mask(pcf, reg, 0x07, val);
}
EXPORT_SYMBOL_GPL(pcf50633_gpio_set);

u8 pcf50633_gpio_get(struct pcf50633 *pcf, int gpio)
{
	u8 reg, val;

	reg = gpio - PCF50633_GPIO1 + PCF50633_REG_GPIO1CFG;
	val = pcf50633_reg_read(pcf, reg) & 0x07;

	return val;
}
EXPORT_SYMBOL_GPL(pcf50633_gpio_get);

int pcf50633_gpio_invert_set(struct pcf50633 *pcf, int gpio, int invert)
{
	u8 val, reg;

	reg = gpio - PCF50633_GPIO1 + PCF50633_REG_GPIO1CFG;
	val = !!invert << 3;

	return pcf50633_reg_set_bit_mask(pcf, reg, 1 << 3, val);
}
EXPORT_SYMBOL_GPL(pcf50633_gpio_invert_set);

int pcf50633_gpio_invert_get(struct pcf50633 *pcf, int gpio)
{
	u8 reg, val;

	reg = gpio - PCF50633_GPIO1 + PCF50633_REG_GPIO1CFG;
	val = pcf50633_reg_read(pcf, reg);

	return val & (1 << 3);
}
EXPORT_SYMBOL_GPL(pcf50633_gpio_invert_get);

int pcf50633_gpio_power_supply_set(struct pcf50633 *pcf,
					int gpio, int regulator, int on)
{
	u8 reg, val, mask;

	/* the *ENA register is always one after the *OUT register */
	reg = pcf50633_regulator_registers[regulator] + 1;

	val = !!on << (gpio - PCF50633_GPIO1);
	mask = 1 << (gpio - PCF50633_GPIO1);

	return pcf50633_reg_set_bit_mask(pcf, reg, mask, val);
}
EXPORT_SYMBOL_GPL(pcf50633_gpio_power_supply_set);
