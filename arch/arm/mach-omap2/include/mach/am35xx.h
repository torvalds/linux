/*:
 * Address mappings and base address for AM35XX specific interconnects
 * and peripherals.
 *
 * Copyright (C) 2009 Texas Instruments
 *
 * Author: Sriramakrishnan <srk@ti.com>
 *	   Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_AM35XX_H
#define __ASM_ARCH_AM35XX_H

/*
 * Base addresses
 *	Note: OMAP3430 IVA2 memory space is being used for AM35xx IPSS modules
 */
#define AM35XX_IPSS_EMAC_BASE		0x5C000000
#define AM35XX_IPSS_USBOTGSS_BASE	0x5C040000
#define AM35XX_IPSS_HECC_BASE		0x5C050000
#define AM35XX_IPSS_VPFE_BASE		0x5C060000


/* HECC module specifc offset definitions */
#define AM35XX_HECC_SCC_HECC_OFFSET	(0x0)
#define AM35XX_HECC_SCC_RAM_OFFSET	(0x3000)
#define AM35XX_HECC_RAM_OFFSET		(0x3000)
#define AM35XX_HECC_MBOX_OFFSET		(0x2000)
#define AM35XX_HECC_INT_LINE		(0x0)
#define AM35XX_HECC_VERSION		(0x1)

#define AM35XX_EMAC_CNTRL_OFFSET	(0x10000)
#define AM35XX_EMAC_CNTRL_MOD_OFFSET	(0x0)
#define AM35XX_EMAC_CNTRL_RAM_OFFSET	(0x20000)
#define AM35XX_EMAC_MDIO_OFFSET		(0x30000)
#define AM35XX_EMAC_CNTRL_RAM_SIZE	(0x2000)
#define AM35XX_EMAC_RAM_ADDR		(AM3517_EMAC_BASE + \
						AM3517_EMAC_CNTRL_RAM_OFFSET)
#define AM35XX_EMAC_HW_RAM_ADDR		(0x01E20000)

#endif  /*  __ASM_ARCH_AM35XX_H */
