/*
 * Copyright (C) 2009 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __ASM_ARCH_DM365_H
#define __ASM_ARCH_DM665_H

#include <linux/platform_device.h>
#include <mach/hardware.h>
#include <mach/emac.h>

#define DM365_EMAC_BASE			(0x01D07000)
#define DM365_EMAC_CNTRL_OFFSET		(0x0000)
#define DM365_EMAC_CNTRL_MOD_OFFSET	(0x3000)
#define DM365_EMAC_CNTRL_RAM_OFFSET	(0x1000)
#define DM365_EMAC_MDIO_OFFSET		(0x4000)
#define DM365_EMAC_CNTRL_RAM_SIZE	(0x2000)

void __init dm365_init(void);

#endif /* __ASM_ARCH_DM365_H */
