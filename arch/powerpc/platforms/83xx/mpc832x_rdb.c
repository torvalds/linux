// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/powerpc/platforms/83xx/mpc832x_rdb.c
 *
 * Copyright (C) Freescale Semiconductor, Inc. 2007. All rights reserved.
 *
 * Description:
 * MPC832x RDB board specific routines.
 * This file is based on mpc832x_mds.c and mpc8313_rdb.c
 * Author: Michael Barkowski <michael.barkowski@freescale.com>
 */

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/spi/mmc_spi.h>
#include <linux/mmc/host.h>
#include <linux/of_platform.h>
#include <linux/fsl_devices.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <soc/fsl/qe/qe.h>
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
static int __init of_fsl_spi_probe(char *type, char *compatible, u32 sysclk,
				   struct spi_board_info *board_infos,
				   unsigned int num_board_infos,
				   void (*cs_control)(struct spi_device *dev,
						      bool on))
{
	struct device_node *np;
	unsigned int i = 0;

	for_each_compatible_node(np, type, compatible) {
		int ret;
		unsigned int j;
		const void *prop;
		struct resource res[2];
		struct platform_device *pdev;
		struct fsl_spi_platform_data pdata = {
			.cs_control = cs_control,
		};

		memset(res, 0, sizeof(res));

		pdata.sysclk = sysclk;

		prop = of_get_property(np, "reg", NULL);
		if (!prop)
			goto err;
		pdata.bus_num = *(u32 *)prop;

		prop = of_get_property(np, "cell-index", NULL);
		if (prop)
			i = *(u32 *)prop;

		prop = of_get_property(np, "mode", NULL);
		if (prop && !strcmp(prop, "cpu-qe"))
			pdata.flags = SPI_QE_CPU_MODE;

		for (j = 0; j < num_board_infos; j++) {
			if (board_infos[j].bus_num == pdata.bus_num)
				pdata.max_chipselect++;
		}

		if (!pdata.max_chipselect)
			continue;

		ret = of_address_to_resource(np, 0, &res[0]);
		if (ret)
			goto err;

		ret = of_irq_to_resource(np, 0, &res[1]);
		if (ret <= 0)
			goto err;

		pdev = platform_device_alloc("mpc83xx_spi", i);
		if (!pdev)
			goto err;

		ret = platform_device_add_data(pdev, &pdata, sizeof(pdata));
		if (ret)
			goto unreg;

		ret = platform_device_add_resources(pdev, res,
						    ARRAY_SIZE(res));
		if (ret)
			goto unreg;

		ret = platform_device_add(pdev);
		if (ret)
			goto unreg;

		goto next;
unreg:
		platform_device_del(pdev);
err:
		pr_err("%pOF: registration failed\n", np);
next:
		i++;
	}

	return i;
}

static int __init fsl_spi_init(struct spi_board_info *board_infos,
			       unsigned int num_board_infos,
			       void (*cs_control)(struct spi_device *spi,
						  bool on))
{
	u32 sysclk = -1;
	int ret;

	/* SPI controller is either clocked from QE or SoC clock */
	sysclk = get_brgfreq();
	if (sysclk == -1) {
		sysclk = fsl_get_sys_freq();
		if (sysclk == -1)
			return -ENODEV;
	}

	ret = of_fsl_spi_probe(NULL, "fsl,spi", sysclk, board_infos,
			       num_board_infos, cs_control);
	if (!ret)
		of_fsl_spi_probe("spi", "fsl_spi", sysclk, board_infos,
				 num_board_infos, cs_control);

	return spi_register_board_info(board_infos, num_board_infos);
}

static void mpc83xx_spi_cs_control(struct spi_device *spi, bool on)
{
	pr_debug("%s %d %d\n", __func__, spi->chip_select, on);
	par_io_data_set(3, 13, on);
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

	/*
	 * Don't bother with legacy stuff when device tree contains
	 * mmc-spi-slot node.
	 */
	if (of_find_compatible_node(NULL, NULL, "mmc-spi-slot"))
		return 0;
	return fsl_spi_init(&mpc832x_spi_boardinfo, 1, mpc83xx_spi_cs_control);
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
#if defined(CONFIG_QUICC_ENGINE)
	struct device_node *np;
#endif

	mpc83xx_setup_arch();

#ifdef CONFIG_QUICC_ENGINE
	if ((np = of_find_node_by_name(NULL, "par_io")) != NULL) {
		par_io_init(np);
		of_node_put(np);

		for_each_node_by_name(np, "ucc")
			par_io_of_config(np);
	}
#endif				/* CONFIG_QUICC_ENGINE */
}

machine_device_initcall(mpc832x_rdb, mpc83xx_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc832x_rdb_probe(void)
{
	return of_machine_is_compatible("MPC832xRDB");
}

define_machine(mpc832x_rdb) {
	.name		= "MPC832x RDB",
	.probe		= mpc832x_rdb_probe,
	.setup_arch	= mpc832x_rdb_setup_arch,
	.init_IRQ	= mpc83xx_ipic_and_qe_init_IRQ,
	.get_irq	= ipic_get_irq,
	.restart	= mpc83xx_restart,
	.time_init	= mpc83xx_time_init,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= udbg_progress,
};
