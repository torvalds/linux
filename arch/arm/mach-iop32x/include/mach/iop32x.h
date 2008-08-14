/*
 * arch/arm/mach-iop32x/include/mach/iop32x.h
 *
 * Intel IOP32X Chip definitions
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __IOP32X_H
#define __IOP32X_H

/*
 * Peripherals that are shared between the iop32x and iop33x but
 * located at different addresses.
 */
#define IOP3XX_GPIO_REG(reg)	(IOP3XX_PERIPHERAL_VIRT_BASE + 0x07c4 + (reg))
#define IOP3XX_TIMER_REG(reg)	(IOP3XX_PERIPHERAL_VIRT_BASE + 0x07e0 + (reg))

#include <asm/hardware/iop3xx.h>

/* ATU Parameters
 * set up a 1:1 bus to physical ram relationship
 * w/ physical ram on top of pci in the memory map
 */
#define IOP32X_MAX_RAM_SIZE            0x40000000UL
#define IOP3XX_MAX_RAM_SIZE            IOP32X_MAX_RAM_SIZE
#define IOP3XX_PCI_LOWER_MEM_BA        0x80000000
#define IOP32X_PCI_MEM_WINDOW_SIZE     0x04000000
#define IOP3XX_PCI_MEM_WINDOW_SIZE     IOP32X_PCI_MEM_WINDOW_SIZE

#endif
