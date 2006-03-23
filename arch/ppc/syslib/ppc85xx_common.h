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

#ifndef __PPC_SYSLIB_PPC85XX_COMMON_H
#define __PPC_SYSLIB_PPC85XX_COMMON_H

#include <linux/config.h>
#include <linux/init.h>

/* Provide access to ccsrbar for any modules, etc */
phys_addr_t get_ccsrbar(void);

#endif /* __PPC_SYSLIB_PPC85XX_COMMON_H */
