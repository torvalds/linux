/*
 * MPC85xx RapidIO definitions
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PPC_SYSLIB_PPC85XX_RIO_H
#define __PPC_SYSLIB_PPC85XX_RIO_H

#include <linux/config.h>
#include <linux/init.h>

extern void mpc85xx_rio_setup(int law_start, int law_size);

#endif				/* __PPC_SYSLIB_PPC85XX_RIO_H */
