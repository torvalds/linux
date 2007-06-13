/*
 * MPC85xx generic code.
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/irq.h>
#include <linux/module.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <sysdev/fsl_soc.h>

static __be32 __iomem *rstcr;

extern void abort(void);

static int __init mpc85xx_rstcr(void)
{
	struct device_node *np;
	np = of_find_node_by_name(NULL, "global-utilities");
	if ((np && of_get_property(np, "fsl,has-rstcr", NULL))) {
		const u32 *prop = of_get_property(np, "reg", NULL);
		if (prop) {
			/* map reset control register
			 * 0xE00B0 is offset of reset control register
			 */
			rstcr = ioremap(get_immrbase() + *prop + 0xB0, 0xff);
			if (!rstcr)
				printk (KERN_EMERG "Error: reset control "
						"register not mapped!\n");
		}
	} else
		printk (KERN_INFO "rstcr compatible register does not exist!\n");
	if (np)
		of_node_put(np);
	return 0;
}

arch_initcall(mpc85xx_rstcr);

void mpc85xx_restart(char *cmd)
{
	local_irq_disable();
	if (rstcr)
		/* set reset control register */
		out_be32(rstcr, 0x2);	/* HRESET_REQ */
	abort();
}
