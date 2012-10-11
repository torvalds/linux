/*
 * arch/arm/mach-ks8695/include/mach/memory.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * KS8695 Memory definitions
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <mach/hardware.h>

/*
 * Physical SRAM offset.
 */
#define PLAT_PHYS_OFFSET		KS8695_SDRAM_PA

#ifndef __ASSEMBLY__

#ifdef CONFIG_PCI

/* PCI mappings */
#define __virt_to_bus(x)	((x) - PAGE_OFFSET + KS8695_PCIMEM_PA)
#define __bus_to_virt(x)	((x) - KS8695_PCIMEM_PA + PAGE_OFFSET)

/* Platform-bus mapping */
extern struct bus_type platform_bus_type;
#define is_lbus_device(dev)		(dev && dev->bus == &platform_bus_type)
#define __arch_dma_to_virt(dev, x)	({ (void *) (is_lbus_device(dev) ? \
					__phys_to_virt(x) : __bus_to_virt(x)); })
#define __arch_virt_to_dma(dev, x)	({ is_lbus_device(dev) ? \
					(dma_addr_t)__virt_to_phys((unsigned long)x) \
					: (dma_addr_t)__virt_to_bus(x); })
#define __arch_pfn_to_dma(dev, pfn)	\
	({ dma_addr_t __dma = __pfn_to_phys(pfn); \
	   if (!is_lbus_device(dev)) \
		__dma = __dma - PHYS_OFFSET + KS8695_PCIMEM_PA; \
	   __dma; })

#define __arch_dma_to_pfn(dev, x)	\
	({ dma_addr_t __dma = x;				\
	   if (!is_lbus_device(dev))				\
		__dma += PHYS_OFFSET - KS8695_PCIMEM_PA;	\
	   __phys_to_pfn(__dma);				\
	})

#endif

#endif

#endif
