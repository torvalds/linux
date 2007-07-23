/*
 * Copyright 2006 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MPC86XX_H__
#define __MPC86XX_H__

/*
 * Declaration for the various functions exported by the
 * mpc86xx_* files. Mostly for use by mpc86xx_setup().
 */

extern int mpc86xx_add_bridge(struct device_node *dev);

extern int mpc86xx_exclude_device(struct pci_controller *hose,
				  u_char bus, u_char devfn);

extern void __init mpc86xx_smp_init(void);

#endif	/* __MPC86XX_H__ */
