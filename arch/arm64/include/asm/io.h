/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/io.h
 *
 * Copyright (C) 1996-2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_IO_H
#define __ASM_IO_H

#include <linux/types.h>
#include <linux/pgtable.h>

#include <asm/byteorder.h>
#include <asm/barrier.h>
#include <asm/memory.h>
#include <asm/early_ioremap.h>
#include <asm/alternative.h>
#include <asm/cpufeature.h>

/*
 * Generic IO read/write.  These perform native-endian accesses.
 */
#define __raw_writeb __raw_writeb
static __always_inline void __raw_writeb(u8 val, volatile void __iomem *addr)
{
	asm volatile("strb %w0, [%1]" : : "rZ" (val), "r" (addr));
}

#define __raw_writew __raw_writew
static __always_inline void __raw_writew(u16 val, volatile void __iomem *addr)
{
	asm volatile("strh %w0, [%1]" : : "rZ" (val), "r" (addr));
}

#define __raw_writel __raw_writel
static __always_inline void __raw_writel(u32 val, volatile void __iomem *addr)
{
	asm volatile("str %w0, [%1]" : : "rZ" (val), "r" (addr));
}

#define __raw_writeq __raw_writeq
static __always_inline void __raw_writeq(u64 val, volatile void __iomem *addr)
{
	asm volatile("str %x0, [%1]" : : "rZ" (val), "r" (addr));
}

#define __raw_readb __raw_readb
static __always_inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 val;
	asm volatile(ALTERNATIVE("ldrb %w0, [%1]",
				 "ldarb %w0, [%1]",
				 ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE)
		     : "=r" (val) : "r" (addr));
	return val;
}

#define __raw_readw __raw_readw
static __always_inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 val;

	asm volatile(ALTERNATIVE("ldrh %w0, [%1]",
				 "ldarh %w0, [%1]",
				 ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE)
		     : "=r" (val) : "r" (addr));
	return val;
}

#define __raw_readl __raw_readl
static __always_inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 val;
	asm volatile(ALTERNATIVE("ldr %w0, [%1]",
				 "ldar %w0, [%1]",
				 ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE)
		     : "=r" (val) : "r" (addr));
	return val;
}

#define __raw_readq __raw_readq
static __always_inline u64 __raw_readq(const volatile void __iomem *addr)
{
	u64 val;
	asm volatile(ALTERNATIVE("ldr %0, [%1]",
				 "ldar %0, [%1]",
				 ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE)
		     : "=r" (val) : "r" (addr));
	return val;
}

/* IO barriers */
#define __io_ar(v)							\
({									\
	unsigned long tmp;						\
									\
	dma_rmb();								\
									\
	/*								\
	 * Create a dummy control dependency from the IO read to any	\
	 * later instructions. This ensures that a subsequent call to	\
	 * udelay() will be ordered due to the ISB in get_cycles().	\
	 */								\
	asm volatile("eor	%0, %1, %1\n"				\
		     "cbnz	%0, ."					\
		     : "=r" (tmp) : "r" ((unsigned long)(v))		\
		     : "memory");					\
})

#define __io_bw()		dma_wmb()
#define __io_br(v)
#define __io_aw(v)

/* arm64-specific, don't use in portable drivers */
#define __iormb(v)		__io_ar(v)
#define __iowmb()		__io_bw()
#define __iomb()		dma_mb()

/*
 *  I/O port access primitives.
 */
#define arch_has_dev_port()	(1)
#define IO_SPACE_LIMIT		(PCI_IO_SIZE - 1)
#define PCI_IOBASE		((void __iomem *)PCI_IO_START)

/*
 * String version of I/O memory access operations.
 */
extern void __memcpy_fromio(void *, const volatile void __iomem *, size_t);
extern void __memcpy_toio(volatile void __iomem *, const void *, size_t);
extern void __memset_io(volatile void __iomem *, int, size_t);

#define memset_io(c,v,l)	__memset_io((c),(v),(l))
#define memcpy_fromio(a,c,l)	__memcpy_fromio((a),(c),(l))
#define memcpy_toio(c,a,l)	__memcpy_toio((c),(a),(l))

/*
 * I/O memory mapping functions.
 */

bool ioremap_allowed(phys_addr_t phys_addr, size_t size, unsigned long prot);
#define ioremap_allowed ioremap_allowed

#define _PAGE_IOREMAP PROT_DEVICE_nGnRE

#define ioremap_wc(addr, size)	\
	ioremap_prot((addr), (size), PROT_NORMAL_NC)
#define ioremap_np(addr, size)	\
	ioremap_prot((addr), (size), PROT_DEVICE_nGnRnE)

/*
 * io{read,write}{16,32,64}be() macros
 */
#define ioread16be(p)		({ __u16 __v = be16_to_cpu((__force __be16)__raw_readw(p)); __iormb(__v); __v; })
#define ioread32be(p)		({ __u32 __v = be32_to_cpu((__force __be32)__raw_readl(p)); __iormb(__v); __v; })
#define ioread64be(p)		({ __u64 __v = be64_to_cpu((__force __be64)__raw_readq(p)); __iormb(__v); __v; })

#define iowrite16be(v,p)	({ __iowmb(); __raw_writew((__force __u16)cpu_to_be16(v), p); })
#define iowrite32be(v,p)	({ __iowmb(); __raw_writel((__force __u32)cpu_to_be32(v), p); })
#define iowrite64be(v,p)	({ __iowmb(); __raw_writeq((__force __u64)cpu_to_be64(v), p); })

#include <asm-generic/io.h>

#define ioremap_cache ioremap_cache
static inline void __iomem *ioremap_cache(phys_addr_t addr, size_t size)
{
	if (pfn_is_map_memory(__phys_to_pfn(addr)))
		return (void __iomem *)__phys_to_virt(addr);

	return ioremap_prot(addr, size, PROT_NORMAL);
}

/*
 * More restrictive address range checking than the default implementation
 * (PHYS_OFFSET and PHYS_MASK taken into account).
 */
#define ARCH_HAS_VALID_PHYS_ADDR_RANGE
extern int valid_phys_addr_range(phys_addr_t addr, size_t size);
extern int valid_mmap_phys_addr_range(unsigned long pfn, size_t size);

extern bool arch_memremap_can_ram_remap(resource_size_t offset, size_t size,
					unsigned long flags);
#define arch_memremap_can_ram_remap arch_memremap_can_ram_remap

#endif	/* __ASM_IO_H */
