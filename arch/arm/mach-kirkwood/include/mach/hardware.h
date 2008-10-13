/*
 * arch/arm/mach-kirkwood/include/mach/hardware.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include "kirkwood.h"

#define pcibios_assign_all_busses()	1

#define PCIBIOS_MIN_IO			0x00001000
#define PCIBIOS_MIN_MEM			0x01000000
#define PCIMEM_BASE			KIRKWOOD_PCIE_MEM_PHYS_BASE /* mem base for VGA */


#endif
