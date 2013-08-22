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

#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg ssp0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = DMA_DEV_TO_MEM,
	.dev_type = DB8500_DMA_DEV8_SSP0,
};

static struct stedma40_chan_cfg ssp0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = DMA_MEM_TO_DEV,
	.dev_type = DB8500_DMA_DEV8_SSP0,
};
#endif

struct pl022_ssp_controller ssp0_plat = {
	.bus_id = 0,
#ifdef CONFIG_STE_DMA40
	.enable_dma = 1,
	.dma_filter = stedma40_filter,
	.dma_rx_param = &ssp0_dma_cfg_rx,
	.dma_tx_param = &ssp0_dma_cfg_tx,
#else
	.enable_dma = 0,
#endif
	/* on this platform, gpio 31,142,144,214 &
	 * 224 are connected as chip selects
	 */
	.num_chipselect = 5,
};

static void __init mop500_init_machine(void)
{
	struct device *parent = NULL;

	platform_device_register(&db8500_prcmu_device);

	parent = u8500_init_devices();

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}


static void __init snowball_init_machine(void)
{
	struct device *parent = NULL;

	platform_device_register(&db8500_prcmu_device);

	parent = u8500_init_devices();

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

static void __init hrefv60_init_machine(void)
{
	struct device *parent = NULL;

	platform_device_register(&db8500_prcmu_device);

	parent = u8500_init_devices();

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

MACHINE_START(U8500, "ST-Ericsson MOP500 platform")
	/* Maintainer: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com> */
	.atag_offset	= 0x100,
	.smp		= smp_ops(ux500_smp_ops),
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.init_time	= ux500_timer_init,
	.init_machine	= mop500_init_machine,
	.init_late	= ux500_init_late,
	.restart        = ux500_restart,
MACHINE_END

MACHINE_START(U8520, "ST-Ericsson U8520 Platform HREFP520")
	.atag_offset	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.init_time	= ux500_timer_init,
	.init_machine	= mop500_init_machine,
	.init_late	= ux500_init_late,
	.restart        = ux500_restart,
MACHINE_END

MACHINE_START(HREFV60, "ST-Ericsson U8500 Platform HREFv60+")
	.atag_offset	= 0x100,
	.smp		= smp_ops(ux500_smp_ops),
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.init_time	= ux500_timer_init,
	.init_machine	= hrefv60_init_machine,
	.init_late	= ux500_init_late,
	.restart        = ux500_restart,
MACHINE_END

MACHINE_START(SNOWBALL, "Calao Systems Snowball platform")
	.atag_offset	= 0x100,
	.smp		= smp_ops(ux500_smp_ops),
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.init_time	= ux500_timer_init,
	.init_machine	= snowball_init_machine,
	.init_late	= NULL,
	.restart        = ux500_restart,
MACHINE_END
