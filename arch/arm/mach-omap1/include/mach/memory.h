/*
 * arch/arm/mach-omap1/include/mach/memory.h
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Bus address is physical address, except for OMAP-1510 Local Bus.
 * OMAP-1510 bus address is translated into a Local Bus address if the
 * OMAP bus type is lbus. We do the address translation based on the
 * device overriding the defaults used in the dma-mapping API.
 * Note that the is_lbus_device() test is not very efficient on 1510
 * because of the strncmp().
 */
#if defined(CONFIG_ARCH_OMAP15XX) && !defined(__ASSEMBLER__)
#include <mach/soc.h>

/*
 * OMAP-1510 Local Bus address offset
 */
#define OMAP1510_LB_OFFSET	UL(0x30000000)

#define virt_to_lbus(x)		((x) - PAGE_OFFSET + OMAP1510_LB_OFFSET)
#define lbus_to_virt(x)		((x) - OMAP1510_LB_OFFSET + PAGE_OFFSET)
#define is_lbus_device(dev)	(cpu_is_omap15xx() && dev && (strncmp(dev_name(dev), "ohci", 4) == 0))

#define __arch_pfn_to_dma(dev, pfn)	\
	({ dma_addr_t __dma = __pfn_to_phys(pfn); \
	   if (is_lbus_device(dev)) \
		__dma = __dma - PHYS_OFFSET + OMAP1510_LB_OFFSET; \
	   __dma; })

#define __arch_dma_to_pfn(dev, addr)	\
	({ dma_addr_t __dma = addr;				\
	   if (is_lbus_device(dev))				\
		__dma += PHYS_OFFSET - OMAP1510_LB_OFFSET;	\
	   __phys_to_pfn(__dma);				\
	})

#define __arch_dma_to_virt(dev, addr)	({ (void *) (is_lbus_device(dev) ? \
						lbus_to_virt(addr) : \
						__phys_to_virt(addr)); })

#define __arch_virt_to_dma(dev, addr)	({ unsigned long __addr = (unsigned long)(addr); \
					   (dma_addr_t) (is_lbus_device(dev) ? \
						virt_to_lbus(__addr) : \
						__virt_to_phys(__addr)); })

#endif	/* CONFIG_ARCH_OMAP15XX */

#endif
