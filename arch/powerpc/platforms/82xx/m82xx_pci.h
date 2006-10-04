#ifndef _PPC_KERNEL_M82XX_PCI_H
#define _PPC_KERNEL_M82XX_PCI_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/m8260_pci.h>

#define SIU_INT_IRQ1   ((uint)0x13 + CPM_IRQ_OFFSET)

#ifndef _IO_BASE
#define _IO_BASE isa_io_base
#endif

#endif				/* _PPC_KERNEL_M8260_PCI_H */
