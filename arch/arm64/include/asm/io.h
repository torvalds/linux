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
#include <asm/rsi.h>

/*
 * Generic IO read/write.  These perform native-endian accesses.
 */
#define __raw_writeb __raw_writeb
static __always_inline void __raw_writeb(u8 val, volatile void __iomem *addr)
{
	volatile u8 __iomem *ptr = addr;
	asm volatile("strb %w0, %1" : : "rZ" (val), "Qo" (*ptr));
}

#define __raw_writew __raw_writew
static __always_inline void __raw_writew(u16 val, volatile void __iomem *addr)
{
	volatile u16 __iomem *ptr = addr;
	asm volatile("strh %w0, %1" : : "rZ" (val), "Qo" (*ptr));
}

#define __raw_writel __raw_writel
static __always_inline void __raw_writel(u32 val, volatile void __iomem *addr)
{
	volatile u32 __iomem *ptr = addr;
	asm volatile("str %w0, %1" : : "rZ" (val), "Qo" (*ptr));
}

#define __raw_writeq __raw_writeq
static __always_inline void __raw_writeq(u64 val, volatile void __iomem *addr)
{
	volatile u64 __iomem *ptr = addr;
	asm volatile("str %x0, %1" : : "rZ" (val), "Qo" (*ptr));
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
 * The ARM64 iowrite implementation is intended to support drivers that want to
 * use write combining. For instance PCI drivers using write combining with a 64
 * byte __iowrite64_copy() expect to get a 64 byte MemWr TLP on the PCIe bus.
 *
 * Newer ARM core have sensitive write combining buffers, it is important that
 * the stores be contiguous blocks of store instructions. Normal memcpy
 * approaches have a very low chance to generate write combining.
 *
 * Since this is the only API on ARM64 that should be used with write combining
 * it also integrates the DGH hint which is supposed to lower the latency to
 * emit the large TLP from the CPU.
 */

static __always_inline void
__const_memcpy_toio_aligned32(volatile u32 __iomem *to, const u32 *from,
			      size_t count)
{
	switch (count) {
	case 8:
		asm volatile("str %w0, [%8, #4 * 0]\n"
			     "str %w1, [%8, #4 * 1]\n"
			     "str %w2, [%8, #4 * 2]\n"
			     "str %w3, [%8, #4 * 3]\n"
			     "str %w4, [%8, #4 * 4]\n"
			     "str %w5, [%8, #4 * 5]\n"
			     "str %w6, [%8, #4 * 6]\n"
			     "str %w7, [%8, #4 * 7]\n"
			     :
			     : "rZ"(from[0]), "rZ"(from[1]), "rZ"(from[2]),
			       "rZ"(from[3]), "rZ"(from[4]), "rZ"(from[5]),
			       "rZ"(from[6]), "rZ"(from[7]), "r"(to));
		break;
	case 4:
		asm volatile("str %w0, [%4, #4 * 0]\n"
			     "str %w1, [%4, #4 * 1]\n"
			     "str %w2, [%4, #4 * 2]\n"
			     "str %w3, [%4, #4 * 3]\n"
			     :
			     : "rZ"(from[0]), "rZ"(from[1]), "rZ"(from[2]),
			       "rZ"(from[3]), "r"(to));
		break;
	case 2:
		asm volatile("str %w0, [%2, #4 * 0]\n"
			     "str %w1, [%2, #4 * 1]\n"
			     :
			     : "rZ"(from[0]), "rZ"(from[1]), "r"(to));
		break;
	case 1:
		__raw_writel(*from, to);
		break;
	default:
		BUILD_BUG();
	}
}

void __iowrite32_copy_full(void __iomem *to, const void *from, size_t count);

static __always_inline void
__iowrite32_copy(void __iomem *to, const void *from, size_t count)
{
	if (__builtin_constant_p(count) &&
	    (count == 8 || count == 4 || count == 2 || count == 1)) {
		__const_memcpy_toio_aligned32(to, from, count);
		dgh();
	} else {
		__iowrite32_copy_full(to, from, count);
	}
}
#define __iowrite32_copy __iowrite32_copy

static __always_inline void
__const_memcpy_toio_aligned64(volatile u64 __iomem *to, const u64 *from,
			      size_t count)
{
	switch (count) {
	case 8:
		asm volatile("str %x0, [%8, #8 * 0]\n"
			     "str %x1, [%8, #8 * 1]\n"
			     "str %x2, [%8, #8 * 2]\n"
			     "str %x3, [%8, #8 * 3]\n"
			     "str %x4, [%8, #8 * 4]\n"
			     "str %x5, [%8, #8 * 5]\n"
			     "str %x6, [%8, #8 * 6]\n"
			     "str %x7, [%8, #8 * 7]\n"
			     :
			     : "rZ"(from[0]), "rZ"(from[1]), "rZ"(from[2]),
			       "rZ"(from[3]), "rZ"(from[4]), "rZ"(from[5]),
			       "rZ"(from[6]), "rZ"(from[7]), "r"(to));
		break;
	case 4:
		asm volatile("str %x0, [%4, #8 * 0]\n"
			     "str %x1, [%4, #8 * 1]\n"
			     "str %x2, [%4, #8 * 2]\n"
			     "str %x3, [%4, #8 * 3]\n"
			     :
			     : "rZ"(from[0]), "rZ"(from[1]), "rZ"(from[2]),
			       "rZ"(from[3]), "r"(to));
		break;
	case 2:
		asm volatile("str %x0, [%2, #8 * 0]\n"
			     "str %x1, [%2, #8 * 1]\n"
			     :
			     : "rZ"(from[0]), "rZ"(from[1]), "r"(to));
		break;
	case 1:
		__raw_writeq(*from, to);
		break;
	default:
		BUILD_BUG();
	}
}

void __iowrite64_copy_full(void __iomem *to, const void *from, size_t count);

static __always_inline void
__iowrite64_copy(void __iomem *to, const void *from, size_t count)
{
	if (__builtin_constant_p(count) &&
	    (count == 8 || count == 4 || count == 2 || count == 1)) {
		__const_memcpy_toio_aligned64(to, from, count);
		dgh();
	} else {
		__iowrite64_copy_full(to, from, count);
	}
}
#define __iowrite64_copy __iowrite64_copy

/*
 * I/O memory mapping functions.
 */

typedef int (*ioremap_prot_hook_t)(phys_addr_t phys_addr, size_t size,
				   pgprot_t *prot);
int arm64_ioremap_prot_hook_register(const ioremap_prot_hook_t hook);

#define ioremap_prot ioremap_prot

#define _PAGE_IOREMAP PROT_DEVICE_nGnRE

#define ioremap_wc(addr, size)	\
	ioremap_prot((addr), (size), __pgprot(PROT_NORMAL_NC))
#define ioremap_np(addr, size)	\
	ioremap_prot((addr), (size), __pgprot(PROT_DEVICE_nGnRnE))


#define ioremap_encrypted(addr, size)	\
	ioremap_prot((addr), (size), PAGE_KERNEL)

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

	return ioremap_prot(addr, size, __pgprot(PROT_NORMAL));
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

static inline bool arm64_is_protected_mmio(phys_addr_t phys_addr, size_t size)
{
	if (unlikely(is_realm_world()))
		return arm64_rsi_is_protected(phys_addr, size);
	return false;
}

#endif	/* __ASM_IO_H */
