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

extern void abort(void);

void mpc85xx_restart(char *cmd)
{
	local_irq_disable();
	abort();
}

/* For now this is a pass through */
phys_addr_t fixup_bigphys_addr(phys_addr_t addr, phys_addr_t size)
{
	return addr;
};

EXPORT_SYMBOL(fixup_bigphys_addr);
