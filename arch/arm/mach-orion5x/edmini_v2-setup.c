/*
 * arch/arm/mach-orion5x/edmini_v2-setup.c
 *
 * LaCie Ethernet Disk mini V2 Setup
 *
 * Copyright (C) 2008 Christopher Moore <moore@free.fr>
 * Copyright (C) 2008 Albert Aribaud <albert.aribaud@free.fr>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/*
 * TODO: add Orion USB device port init when kernel.org support is added.
 * TODO: add flash write support: see below.
 * TODO: add power-off support.
 * TODO: add I2C EEPROM support.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mbus.h>
#include <linux/mtd/physmap.h>
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * EDMINI_V2 Info
 ****************************************************************************/

/*****************************************************************************
 * General Setup
 ****************************************************************************/

void __init edmini_v2_init(void)
{
	pr_notice("edmini_v2: USB device port, flash write and power-off "
		  "are not yet supported.\n");
}
