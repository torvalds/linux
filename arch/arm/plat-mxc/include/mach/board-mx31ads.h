/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_BOARD_MX31ADS_H__
#define __ASM_ARCH_MXC_BOARD_MX31ADS_H__

#include <mach/hardware.h>

/*
 * These symbols are used by drivers/net/cs89x0.c.
 * This is ugly as hell, but we have to provide them until
 * someone fixed the driver.
 */

/* Base address of PBC controller */
#define PBC_BASE_ADDRESS        MX31_CS4_BASE_ADDR_VIRT
/* Offsets for the PBC Controller register */

/* Ethernet Controller IO base address */
#define PBC_CS8900A_IOBASE      0x020000

#define MXC_EXP_IO_BASE		(MXC_BOARD_IRQ_START)

#define EXPIO_INT_ENET_INT	(MXC_EXP_IO_BASE + 8)

#endif /* __ASM_ARCH_MXC_BOARD_MX31ADS_H__ */
