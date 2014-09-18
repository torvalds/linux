/*
 * Keymile km82xx support
 * Copyright 2008-2011 DENX Software Engineering GmbH
 * Author: Heiko Schocher <hs@denx.de>
 *
 * based on code from:
 * Copyright 2007 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fsl_devices.h>
#include <linux/of_platform.h>

#include <linux/io.h>
#include <asm/cpm2.h>
#include <asm/udbg.h>
#include <asm/machdep.h>
#include <linux/time.h>
#include <asm/mpc8260.h>
#include <asm/prom.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/cpm2_pic.h>

#include "pq2.h"

static void __init km82xx_pic_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL,
							"fsl,pq2-pic");
	if (!np) {
		pr_err("PIC init: can not find cpm-pic node\n");
		return;
	}

	cpm2_pic_init(np);
	of_node_put(np);
}

struct cpm_pin {
	int port, pin, flags;
};

static __initdata struct cpm_pin km82xx_pins[] = {
	/* SMC1 */
	{2, 4, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 5, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},

	/* SMC2 */
	{0, 8, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 9, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},

	/* SCC1 */
	{2, 21, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 15, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{3, 31, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{3, 30, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},

	/* SCC4 */
	{2, 25, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 24, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2,  9, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2,  8, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{3, 22, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{3, 21, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},

	/* FCC1 */
	{0, 14, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 15, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 16, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 17, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 18, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 19, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 20, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 21, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 26, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 27, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 28, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{0, 29, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{0, 30, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 31, CPM_PIN_INPUT | CPM_PIN_SECONDARY},

	{2, 22, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 23, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* FCC2 */
	{1, 18, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 19, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 20, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 21, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 22, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 23, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 24, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 25, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 26, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 27, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 28, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 29, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{1, 30, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 31, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},

	{2, 18, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 19, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* MDC */
	{0, 13, CPM_PIN_OUTPUT | CPM_PIN_GPIO},

#if defined(CONFIG_I2C_CPM)
	/* I2C */
	{3, 14, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_OPENDRAIN},
	{3, 15, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_OPENDRAIN},
#endif

	/* USB */
	{0, 10, CPM_PIN_OUTPUT | CPM_PIN_GPIO},    /* FULL_SPEED */
	{0, 11, CPM_PIN_OUTPUT | CPM_PIN_GPIO},    /*/SLAVE */
	{2, 10, CPM_PIN_INPUT  | CPM_PIN_PRIMARY}, /* RXN */
	{2, 11, CPM_PIN_INPUT  | CPM_PIN_PRIMARY}, /* RXP */
	{2, 20, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY}, /* /OE */
	{2, 27, CPM_PIN_INPUT  | CPM_PIN_PRIMARY}, /* RXCLK */
	{3, 23, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY}, /* TXP */
	{3, 24, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY}, /* TXN */
	{3, 25, CPM_PIN_INPUT  | CPM_PIN_PRIMARY}, /* RXD */

	/* SPI */
	{3, 16, CPM_PIN_INPUT | CPM_PIN_SECONDARY},/* SPI_MISO PD16 */
	{3, 17, CPM_PIN_INPUT | CPM_PIN_SECONDARY},/* SPI_MOSI PD17 */
	{3, 18, CPM_PIN_INPUT | CPM_PIN_SECONDARY},/* SPI_CLK PD18 */
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(km82xx_pins); i++) {
		const struct cpm_pin *pin = &km82xx_pins[i];
		cpm2_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm2_smc_clk_setup(CPM_CLK_SMC2, CPM_BRG8);
	cpm2_smc_clk_setup(CPM_CLK_SMC1, CPM_BRG7);
	cpm2_clk_setup(CPM_CLK_SCC1, CPM_CLK11, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC1, CPM_CLK11, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_SCC3, CPM_CLK5, CPM_CLK_RTX);
	cpm2_clk_setup(CPM_CLK_SCC4, CPM_CLK7, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC4, CPM_CLK8, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC1, CPM_CLK10, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC1, CPM_CLK9,  CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK13, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK14, CPM_CLK_TX);

	/* Force USB FULL SPEED bit to '1' */
	setbits32(&cpm2_immr->im_ioport.iop_pdata, 1 << (31 - 10));
	/* clear USB_SLAVE */
	clrbits32(&cpm2_immr->im_ioport.iop_pdata, 1 << (31 - 11));
}

static void __init km82xx_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("km82xx_setup_arch()", 0);

	cpm2_reset();

	/* When this is set, snooping CPM DMA from RAM causes
	 * machine checks.  See erratum SIU18.
	 */
	clrbits32(&cpm2_immr->im_siu_conf.siu_82xx.sc_bcr, MPC82XX_BCR_PLDP);

	init_ioports();

	if (ppc_md.progress)
		ppc_md.progress("km82xx_setup_arch(), finish", 0);
}

static  __initdata struct of_device_id of_bus_ids[] = {
	{ .compatible = "simple-bus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(km82xx, declare_of_platform_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init km82xx_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	return of_flat_dt_is_compatible(root, "keymile,km82xx");
}

define_machine(km82xx)
{
	.name = "Keymile km82xx",
	.probe = km82xx_probe,
	.setup_arch = km82xx_setup_arch,
	.init_IRQ = km82xx_pic_init,
	.get_irq = cpm2_get_irq,
	.calibrate_decr = generic_calibrate_decr,
	.restart = pq2_restart,
	.progress = udbg_progress,
};
