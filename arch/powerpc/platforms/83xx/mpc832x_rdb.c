/*
 * arch/powerpc/platforms/83xx/mpc832x_rdb.c
 *
 * Copyright (C) Freescale Semiconductor, Inc. 2007. All rights reserved.
 *
 * Description:
 * MPC832x RDB board specific routines.
 * This file is based on mpc832x_mds.c and mpc8313_rdb.c
 * Author: Michael Barkowski <michael.barkowski@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/spi/mmc_spi.h>
#include <linux/mmc/host.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <asm/qe.h>
#include <asm/qe_ic.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

#undef DEBUG
#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

#ifdef CONFIG_QUICC_ENGINE
static void mpc83xx_spi_activate_cs(u8 cs, u8 polarity)
{
	pr_debug("%s %d %d\n", __func__, cs, polarity);
	par_io_data_set(3, 13, polarity);
}

static void mpc83xx_spi_deactivate_cs(u8 cs, u8 polarity)
{
	pr_debug("%s %d %d\n", __func__, cs, polarity);
	par_io_data_set(3, 13, !polarity);
}

static struct mmc_spi_platform_data mpc832x_mmc_pdata = {
	.ocr_mask = MMC_VDD_33_34,
};

static struct spi_board_info mpc832x_spi_boardinfo = {
	.bus_num = 0x4c0,
	.chip_select = 0,
	.max_speed_hz = 50000000,
	.modalias = "mmc_spi",
	.platform_data = &mpc832x_mmc_pdata,
};

static int __init mpc832x_spi_init(void)
{
	par_io_config_pin(3,  0, 3, 0, 1, 0); /* SPI1 MOSI, I/O */
	par_io_config_pin(3,  1, 3, 0, 1, 0); /* SPI1 MISO, I/O */
	par_io_config_pin(3,  2, 3, 0, 1, 0); /* SPI1 CLK,  I/O */
	par_io_config_pin(3,  3, 2, 0, 1, 0); /* SPI1 SEL,  I   */

	par_io_config_pin(3, 13, 1, 0, 0, 0); /* !SD_CS,    O */
	par_io_config_pin(3, 14, 2, 0, 0, 0); /* SD_INSERT, I */
	par_io_config_pin(3, 15, 2, 0, 0, 0); /* SD_PROTECT,I */

	return fsl_spi_init(&mpc832x_spi_boardinfo, 1,
			    mpc83xx_spi_activate_cs,
			    mpc83xx_spi_deactivate_cs);
}
machine_device_initcall(mpc832x_rdb, mpc832x_spi_init);
#endif /* CONFIG_QUICC_ENGINE */

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc832x_rdb_setup_arch(void)
{
#if defined(CONFIG_PCI) || defined(CONFIG_QUICC_ENGINE)
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc832x_rdb_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8349-pci")
		mpc83xx_add_bridge(np);
#endif

#ifdef CONFIG_QUICC_ENGINE
	qe_reset();

	if ((np = of_find_node_by_name(NULL, "par_io")) != NULL) {
		par_io_init(np);
		of_node_put(np);

		for (np = NULL; (np = of_find_node_by_name(np, "ucc")) != NULL;)
			par_io_of_config(np);
	}
#endif				/* CONFIG_QUICC_ENGINE */
}

static struct of_device_id mpc832x_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{ .type = "qe", },
	{ .compatible = "fsl,qe", },
	{},
};

static int __init mpc832x_declare_of_platform_devices(void)
{
	/* Publish the QE devices */
	of_platform_bus_probe(NULL, mpc832x_ids, NULL);

	return 0;
}
machine_device_initcall(mpc832x_rdb, mpc832x_declare_of_platform_devices);

static void __init mpc832x_rdb_init_IRQ(void)
{

	struct device_node *np;

	np = of_find_node_by_type(NULL, "ipic");
	if (!np)
		return;

	ipic_init(np, 0);

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	ipic_set_default_priority();
	of_node_put(np);

#ifdef CONFIG_QUICC_ENGINE
	np = of_find_compatible_node(NULL, NULL, "fsl,qe-ic");
	if (!np) {
		np = of_find_node_by_type(NULL, "qeic");
		if (!np)
			return;
	}
	qe_ic_init(np, 0, qe_ic_cascade_low_ipic, qe_ic_cascade_high_ipic);
	of_node_put(np);
#endif				/* CONFIG_QUICC_ENGINE */
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc832x_rdb_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "MPC832xRDB");
}

define_machine(mpc832x_rdb) {
	.name		= "MPC832x RDB",
	.probe		= mpc832x_rdb_probe,
	.setup_arch	= mpc832x_rdb_setup_arch,
	.init_IRQ	= mpc832x_rdb_init_IRQ,
	.get_irq	= ipic_get_irq,
	.restart	= mpc83xx_restart,
	.time_init	= mpc83xx_time_init,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= udbg_progress,
};
