/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OMAP Traffic Controller
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 */

#ifndef __ASM_ARCH_TC_H
#define __ASM_ARCH_TC_H

#define TCMIF_BASE		0xfffecc00
#define OMAP_TC_OCPT1_PRIOR	(TCMIF_BASE + 0x00)
#define OMAP_TC_EMIFS_PRIOR	(TCMIF_BASE + 0x04)
#define OMAP_TC_EMIFF_PRIOR	(TCMIF_BASE + 0x08)
#define EMIFS_CONFIG		(TCMIF_BASE + 0x0c)
#define EMIFS_CS0_CONFIG	(TCMIF_BASE + 0x10)
#define EMIFS_CS1_CONFIG	(TCMIF_BASE + 0x14)
#define EMIFS_CS2_CONFIG	(TCMIF_BASE + 0x18)
#define EMIFS_CS3_CONFIG	(TCMIF_BASE + 0x1c)
#define EMIFF_SDRAM_CONFIG	(TCMIF_BASE + 0x20)
#define EMIFF_MRS		(TCMIF_BASE + 0x24)
#define TC_TIMEOUT1		(TCMIF_BASE + 0x28)
#define TC_TIMEOUT2		(TCMIF_BASE + 0x2c)
#define TC_TIMEOUT3		(TCMIF_BASE + 0x30)
#define TC_ENDIANISM		(TCMIF_BASE + 0x34)
#define EMIFF_SDRAM_CONFIG_2	(TCMIF_BASE + 0x3c)
#define EMIF_CFG_DYNAMIC_WS	(TCMIF_BASE + 0x40)
#define EMIFS_ACS0		(TCMIF_BASE + 0x50)
#define EMIFS_ACS1		(TCMIF_BASE + 0x54)
#define EMIFS_ACS2		(TCMIF_BASE + 0x58)
#define EMIFS_ACS3		(TCMIF_BASE + 0x5c)
#define OMAP_TC_OCPT2_PRIOR	(TCMIF_BASE + 0xd0)

/* external EMIFS chipselect regions */
#define	OMAP_CS0_PHYS		0x00000000
#define	OMAP_CS0_SIZE		SZ_64M

#define	OMAP_CS1_PHYS		0x04000000
#define	OMAP_CS1_SIZE		SZ_64M

#define	OMAP_CS1A_PHYS		OMAP_CS1_PHYS
#define	OMAP_CS1A_SIZE		SZ_32M

#define	OMAP_CS1B_PHYS		(OMAP_CS1A_PHYS + OMAP_CS1A_SIZE)
#define	OMAP_CS1B_SIZE		SZ_32M

#define	OMAP_CS2_PHYS		0x08000000
#define	OMAP_CS2_SIZE		SZ_64M

#define	OMAP_CS2A_PHYS		OMAP_CS2_PHYS
#define	OMAP_CS2A_SIZE		SZ_32M

#define	OMAP_CS2B_PHYS		(OMAP_CS2A_PHYS + OMAP_CS2A_SIZE)
#define	OMAP_CS2B_SIZE		SZ_32M

#define	OMAP_CS3_PHYS		0x0c000000
#define	OMAP_CS3_SIZE		SZ_64M

#ifndef	__ASSEMBLER__

/* EMIF Slow Interface Configuration Register */
#define OMAP_EMIFS_CONFIG_FR		(1 << 4)
#define OMAP_EMIFS_CONFIG_PDE		(1 << 3)
#define OMAP_EMIFS_CONFIG_PWD_EN	(1 << 2)
#define OMAP_EMIFS_CONFIG_BM		(1 << 1)
#define OMAP_EMIFS_CONFIG_WP		(1 << 0)

#define EMIFS_CCS(n)		(EMIFS_CS0_CONFIG + (4 * (n)))
#define EMIFS_ACS(n)		(EMIFS_ACS0 + (4 * (n)))

#endif	/* __ASSEMBLER__ */

#endif	/* __ASM_ARCH_TC_H */
