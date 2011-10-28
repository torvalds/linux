/*
 * Copyright 2003 ARM Limited
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_MEMORY_H
#define __MACH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PLAT_PHYS_OFFSET		UL(0x00000000)

#define __phys_to_bus(x)	((x) + PHYS_OFFSET)
#define __bus_to_phys(x)	((x) - PHYS_OFFSET)

#define __virt_to_bus(v)	__phys_to_bus(__virt_to_phys(v))
#define __bus_to_virt(b)	__phys_to_virt(__bus_to_phys(b))
#define __pfn_to_bus(p)		__phys_to_bus(__pfn_to_phys(p))
#define __bus_to_pfn(b)		__phys_to_pfn(__bus_to_phys(b))

#endif
