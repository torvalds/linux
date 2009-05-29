/*
 * Chip specific defines for DM646x SoC
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_DM646X_H
#define __ASM_ARCH_DM646X_H

#include <mach/hardware.h>
#include <mach/emac.h>

#define DM646X_EMAC_BASE		(0x01C80000)
#define DM646X_EMAC_CNTRL_OFFSET	(0x0000)
#define DM646X_EMAC_CNTRL_MOD_OFFSET	(0x1000)
#define DM646X_EMAC_CNTRL_RAM_OFFSET	(0x2000)
#define DM646X_EMAC_MDIO_OFFSET		(0x4000)
#define DM646X_EMAC_CNTRL_RAM_SIZE	(0x2000)

void __init dm646x_init(void);

#endif /* __ASM_ARCH_DM646X_H */
