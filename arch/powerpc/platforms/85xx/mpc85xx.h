/*
 * arch/ppc/platforms/85xx/mpc85xx.h
 *
 * MPC85xx soc definitions/function decls
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

extern void mpc85xx_restart(char *);
extern int add_bridge(struct device_node *dev);
