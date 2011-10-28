#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <mach/hardware.h>

/*
 * Physical DRAM offset.
 */
#define PLAT_PHYS_OFFSET	UL(0x00000000)

#ifndef __ASSEMBLY__

#if defined(CONFIG_ARCH_IOP13XX)
#define IOP13XX_PMMR_V_START (IOP13XX_PMMR_VIRT_MEM_BASE)
#define IOP13XX_PMMR_V_END   (IOP13XX_PMMR_VIRT_MEM_BASE + IOP13XX_PMMR_SIZE)
#define IOP13XX_PMMR_P_START (IOP13XX_PMMR_PHYS_MEM_BASE)
#define IOP13XX_PMMR_P_END   (IOP13XX_PMMR_PHYS_MEM_BASE + IOP13XX_PMMR_SIZE)

static inline dma_addr_t __virt_to_lbus(unsigned long x)
{
	return x + IOP13XX_PMMR_PHYS_MEM_BASE - IOP13XX_PMMR_VIRT_MEM_BASE;
}

static inline unsigned long __lbus_to_virt(dma_addr_t x)
{
	return x + IOP13XX_PMMR_VIRT_MEM_BASE - IOP13XX_PMMR_PHYS_MEM_BASE;
}

#define __is_lbus_dma(a)				\
	((a) >= IOP13XX_PMMR_P_START && (a) < IOP13XX_PMMR_P_END)

#define __is_lbus_virt(a)				\
	((a) >= IOP13XX_PMMR_V_START && (a) < IOP13XX_PMMR_V_END)

/* Device is an lbus device if it is on the platform bus of the IOP13XX */
#define is_lbus_device(dev) 				\
	(dev && strncmp(dev->bus->name, "platform", 8) == 0)

#define __arch_dma_to_virt(dev, addr)					\
	({								\
		unsigned long __virt;					\
		dma_addr_t __dma = addr;				\
		if (is_lbus_device(dev) && __is_lbus_dma(__dma))	\
			__virt = __lbus_to_virt(__dma);			\
		else							\
			__virt = __phys_to_virt(__dma);			\
		(void *)__virt;						\
	})

#define __arch_virt_to_dma(dev, addr)					\
	({								\
		unsigned long __virt = (unsigned long)addr;		\
		dma_addr_t __dma;					\
		if (is_lbus_device(dev) && __is_lbus_virt(__virt))	\
			__dma = __virt_to_lbus(__virt);			\
		else							\
			__dma = __virt_to_phys(__virt);			\
		__dma;							\
	})

#define __arch_pfn_to_dma(dev, pfn)					\
	({								\
		/* __is_lbus_virt() can never be true for RAM pages */	\
		(dma_addr_t)__pfn_to_phys(pfn);				\
	})

#define __arch_dma_to_pfn(dev, addr)	__phys_to_pfn(addr)

#endif /* CONFIG_ARCH_IOP13XX */
#endif /* !ASSEMBLY */

#endif
