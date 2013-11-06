/*
 * Copyright (C) 2008-2012 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_data/db8500_thermal.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/regulator/ab8500.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/tps6105x.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_data/pinctrl-nomadik.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include <asm/mach-types.h>

#include "setup.h"
#include "devices.h"
#include "irqs.h"

#include "ste-dma40-db8500.h"
#include "db8500-regs.h"
#include "devices-db8500.h"
#include "board-mop500.h"
#include "board-mop500-regulators.h"

struct ab8500_platform_data ab8500_platdata = {
	.irq_base	= MOP500_AB8500_IRQ_BASE,
	.regulator	= &ab8500_regulator_plat_data,
};
