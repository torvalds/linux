#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <mach/hardware.h>

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	UL(0x00000000)

#ifndef __ASSEMBLY__

#if defined(CONFIG_ARCH_IOP13XX)
#define IOP13XX_PMMR_V_START (IOP13XX_PMMR_VIRT_MEM_BASE)
#define IOP13XX_PMMR_V_END   (IOP13XX_PMMR_VIRT_MEM_BASE + IOP13XX_PMMR_SIZE)
#define IOP13XX_PMMR_P_START (IOP13XX_PMMR_PHYS_MEM_BASE)
#define IOP13XX_PMMR_P_END   (IOP13XX_PMMR_PHYS_MEM_BASE + IOP13XX_PMMR_SIZE)

/*
 * Virtual view <-> PCI DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 */

/* RAM has 1:1 mapping on the PCIe/x Busses */
#define __virt_to_bus(x)	(__virt_to_phys(x))
#define __bus_to_virt(x)    (__phys_to_virt(x))

#define virt_to_lbus(x) 					   \
(( ((void*)(x) >= (void*)IOP13XX_PMMR_V_START) &&		   \
((void*)(x) < (void*)IOP13XX_PMMR_V_END) ) ? 			   \
((x) - IOP13XX_PMMR_VIRT_MEM_BASE + IOP13XX_PMMR_PHYS_MEM_BASE) : \
((x) - PAGE_OFFSET + PHYS_OFFSET))

#define lbus_to_virt(x)                                            \
(( ((x) >= IOP13XX_PMMR_P_START) && ((x) < IOP13XX_PMMR_P_END) ) ? \
((x) - IOP13XX_PMMR_PHYS_MEM_BASE + IOP13XX_PMMR_VIRT_MEM_BASE ) : \
((x) - PHYS_OFFSET + PAGE_OFFSET))

/* Device is an lbus device if it is on the platform bus of the IOP13XX */
#define is_lbus_device(dev) (dev &&\
			     (strncmp(dev->bus->name, "platform", 8) == 0))

#define __arch_page_to_dma(dev, page)					\
({is_lbus_device(dev) ? (dma_addr_t)virt_to_lbus(page_address(page)) : \
(dma_addr_t)__virt_to_bus(page_address(page));})

#define __arch_dma_to_virt(dev, addr) \
({is_lbus_device(dev) ? lbus_to_virt(addr) : __bus_to_virt(addr);})

#define __arch_virt_to_dma(dev, addr) \
({is_lbus_device(dev) ? virt_to_lbus(addr) : __virt_to_bus(addr);})

#endif /* CONFIG_ARCH_IOP13XX */
#endif /* !ASSEMBLY */

#define PFN_TO_NID(addr)	(0)

#endif
