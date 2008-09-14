/*
 * arch/arm/mach-ixp4xx/include/mach/prpmc1100.h
 *
 * Motorolla PrPMC1100 platform specific definitions
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2004 (c) MontaVista, Software, Inc. 
 * 
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#error "Do not include this directly, instead #include <mach/hardware.h>"
#endif

#define	PRPMC1100_FLASH_BASE	IXP4XX_EXP_BUS_CS0_BASE_PHYS
#define	PRPMC1100_FLASH_SIZE	IXP4XX_EXP_BUS_CSX_REGION_SIZE

#define	PRPMC1100_PCI_MIN_DEVID	10
#define	PRPMC1100_PCI_MAX_DEVID	16
#define	PRPMC1100_PCI_IRQ_LINES	4


/* PCI controller GPIO to IRQ pin mappings */
#define PRPMC1100_PCI_INTA_PIN	11
#define PRPMC1100_PCI_INTB_PIN	10
#define	PRPMC1100_PCI_INTC_PIN	9
#define	PRPMC1100_PCI_INTD_PIN	8


