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

extern int add_bridge(struct device_node *dev);

extern int mpc86xx_exclude_device(u_char bus, u_char devfn);

extern void setup_indirect_pcie(struct pci_controller *hose,
				       u32 cfg_addr, u32 cfg_data);
extern void setup_indirect_pcie_nomap(struct pci_controller *hose,
					     void __iomem *cfg_addr,
					     void __iomem *cfg_data);

extern void __init mpc86xx_smp_init(void);

#endif	/* __MPC86XX_H__ */
