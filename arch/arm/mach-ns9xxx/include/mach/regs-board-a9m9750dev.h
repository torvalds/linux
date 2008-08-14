/*
 * arch/arm/mach-ns9xxx/include/mach/regs-board-a9m9750dev.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_REGSBOARDA9M9750_H
#define __ASM_ARCH_REGSBOARDA9M9750_H

#include <mach/hardware.h>

#define FPGA_UARTA_BASE	io_p2v(NS9XXX_CSxSTAT_PHYS(0))
#define FPGA_UARTB_BASE	io_p2v(NS9XXX_CSxSTAT_PHYS(0) + 0x08)
#define FPGA_UARTC_BASE	io_p2v(NS9XXX_CSxSTAT_PHYS(0) + 0x10)
#define FPGA_UARTD_BASE	io_p2v(NS9XXX_CSxSTAT_PHYS(0) + 0x18)

#define FPGA_IER	__REG(NS9XXX_CSxSTAT_PHYS(0) + 0x50)
#define FPGA_ISR	__REG(NS9XXX_CSxSTAT_PHYS(0) + 0x60)

#endif /* ifndef __ASM_ARCH_REGSBOARDA9M9750_H */
