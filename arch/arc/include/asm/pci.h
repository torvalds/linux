/*
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_PCI_H
#define _ASM_ARC_PCI_H

#ifdef __KERNEL__
#include <linux/ioport.h>

#define PCIBIOS_MIN_IO 0x100
#define PCIBIOS_MIN_MEM 0x100000

#define pcibios_assign_all_busses()	1
/*
 * The PCI address space does equal the physical memory address space.
 * The networking and block device layers use this boolean for bounce
 * buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	1

#endif /* __KERNEL__ */

#endif /* _ASM_ARC_PCI_H */
