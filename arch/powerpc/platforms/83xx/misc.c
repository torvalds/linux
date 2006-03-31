/*
 * misc setup functions for MPC83xx
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>

#include <asm/io.h>
#include <asm/hw_irq.h>
#include <sysdev/fsl_soc.h>

#include "mpc83xx.h"

void mpc83xx_restart(char *cmd)
{
#define RST_OFFSET	0x00000900
#define RST_PROT_REG	0x00000018
#define RST_CTRL_REG	0x0000001c
	__be32 __iomem *reg;

	/* map reset register space */
	reg = ioremap(get_immrbase() + 0x900, 0xff);

	local_irq_disable();

	/* enable software reset "RSTE" */
	out_be32(reg + (RST_PROT_REG >> 2), 0x52535445);

	/* set software hard reset */
	out_be32(reg + (RST_CTRL_REG >> 2), 0x2);
	for (;;) ;
}

long __init mpc83xx_time_init(void)
{
#define SPCR_OFFSET	0x00000110
#define SPCR_TBEN	0x00400000
	__be32 __iomem *spcr = ioremap(get_immrbase() + SPCR_OFFSET, 4);
	__be32 tmp;

	tmp = in_be32(spcr);
	out_be32(spcr, tmp | SPCR_TBEN);

	iounmap(spcr);

	return 0;
}
