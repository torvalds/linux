/*
 * MPC85xx support routines
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/mpc85xx.h>
#include <asm/mmu.h>

/* ************************************************************************ */
/* Return the value of CCSRBAR for the current board */

phys_addr_t
get_ccsrbar(void)
{
        return BOARD_CCSRBAR;
}

EXPORT_SYMBOL(get_ccsrbar);

/* For now this is a pass through */
phys_addr_t fixup_bigphys_addr(phys_addr_t addr, phys_addr_t size)
{
	return addr;
};
EXPORT_SYMBOL(fixup_bigphys_addr);

