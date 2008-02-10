/*
 * linux/arch/arm/mach-omap1/board-sx1-mmc.c
 *
 * Copyright (C) 2007 Instituto Nokia de Tecnologia - INdT
 * Author: Carlos Eduardo Aguiar <carlos.aguiar@indt.org.br>
 *
 * This code is based on linux/arch/arm/mach-omap1/board-h2-mmc.c, which is:
 * Copyright (C) 2007 Instituto Nokia de Tecnologia - INdT
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/arch/hardware.h>
#include <asm/arch/mmc.h>
#include <asm/arch/gpio.h>

#ifdef CONFIG_MMC_OMAP
static int slot_cover_open;
static struct device *mmc_device;

static int sx1_mmc_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	int err;
	u8 dat = 0;

#ifdef CONFIG_MMC_DEBUG
	dev_dbg(dev, "Set slot %d power: %s (vdd %d)\n", slot + 1,
		power_on ? "on" : "off", vdd);
#endif

	if (slot != 0) {
		dev_err(dev, "No such slot %d\n", slot + 1);
		return -ENODEV;
	}

	err = sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, &dat);
	if (err < 0)
		return err;

	if (power_on)
		dat |= SOFIA_MMC_POWER;
	else
		dat &= ~SOFIA_MMC_POWER;

	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, dat);
}

static int sx1_mmc_set_bus_mode(struct device *dev, int slot, int bus_mode)
{
#ifdef CONFIG_MMC_DEBUG
	dev_dbg(dev, "Set slot %d bus_mode %s\n", slot + 1,
		bus_mode == MMC_BUSMODE_OPENDRAIN ? "open-drain" : "push-pull");
#endif
	if (slot != 0) {
		dev_err(dev, "No such slot %d\n", slot + 1);
		return -ENODEV;
	}

	return 0;
}

static int sx1_mmc_get_cover_state(struct device *dev, int slot)
{
	BUG_ON(slot != 0);

	return slot_cover_open;
}

void sx1_mmc_slot_cover_handler(void *arg, int state)
{
	if (mmc_device == NULL)
		return;

	slot_cover_open = state;
	omap_mmc_notify_cover_event(mmc_device, 0, state);
}

static int sx1_mmc_late_init(struct device *dev)
{
	int ret = 0;

	mmc_device = dev;

	return ret;
}

static void sx1_mmc_cleanup(struct device *dev)
{
}

static struct omap_mmc_platform_data sx1_mmc_data = {
	.nr_slots                       = 1,
	.switch_slot                    = NULL,
	.init                           = sx1_mmc_late_init,
	.cleanup                        = sx1_mmc_cleanup,
	.slots[0]       = {
		.set_power              = sx1_mmc_set_power,
		.set_bus_mode           = sx1_mmc_set_bus_mode,
		.get_ro                 = NULL,
		.get_cover_state        = sx1_mmc_get_cover_state,
		.ocr_mask               = MMC_VDD_28_29 | MMC_VDD_30_31 |
					  MMC_VDD_32_33 | MMC_VDD_33_34,
		.name                   = "mmcblk",
	},
};

void __init sx1_mmc_init(void)
{
	omap_set_mmc_info(1, &sx1_mmc_data);
}

#else

void __init sx1_mmc_init(void)
{
}

void sx1_mmc_slot_cover_handler(void *arg, int state)
{
}
#endif
