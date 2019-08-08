/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-iop33x/include/mach/iop33x.h
 *
 * Intel IOP33X Chip definitions
 *
 * Author: Dave Jiang (dave.jiang@intel.com)
 * Copyright (C) 2003, 2004 Intel Corp.
 */

#ifndef __IOP33X_H
#define __IOP33X_H

/*
 * Peripherals that are shared between the iop32x and iop33x but
 * located at different addresses.
 */
#define IOP3XX_TIMER_REG(reg)	(IOP3XX_PERIPHERAL_VIRT_BASE + 0x07d0 + (reg))

#include <asm/hardware/iop3xx.h>

/* UARTs  */
#define IOP33X_UART0_PHYS	(IOP3XX_PERIPHERAL_PHYS_BASE + 0x1700)
#define IOP33X_UART0_VIRT	(IOP3XX_PERIPHERAL_VIRT_BASE + 0x1700)
#define IOP33X_UART1_PHYS	(IOP3XX_PERIPHERAL_PHYS_BASE + 0x1740)
#define IOP33X_UART1_VIRT	(IOP3XX_PERIPHERAL_VIRT_BASE + 0x1740)

/* ATU Parameters
 * set up a 1:1 bus to physical ram relationship
 * w/ pci on top of physical ram in memory map
 */
#define IOP33X_MAX_RAM_SIZE		0x80000000UL
#define IOP3XX_MAX_RAM_SIZE		IOP33X_MAX_RAM_SIZE
#define IOP3XX_PCI_LOWER_MEM_BA	(PHYS_OFFSET + IOP33X_MAX_RAM_SIZE)


#endif
