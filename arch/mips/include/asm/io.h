/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf GmbH
 * Copyright (C) 1994 - 2000, 06 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2004, 2005  MIPS Technologies, Inc.  All rights reserved.
 *	Author: Maciej W. Rozycki <macro@mips.com>
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#define ARCH_HAS_IOREMAP_WC

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/irqflags.h>

#include <asm/addrspace.h>
#include <asm/bug.h>
#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm-generic/iomap.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/processor.h>
#include <asm/string.h>

#include <ioremap.h>
#include <mangle-port.h>

/*
 * Slowdown I/O port space accesses for antique hardware.
 */
#undef CONF_SLOWDOWN_IO

/*
 * Raw operations are never swapped in software.  OTOH values that raw
 * operations are working on may or may not have been swapped by the bus
 * hardware.  An example use would be for flash memory that's used for
 * execute in place.
 */
# define __raw_ioswabb(a, x)	(x)
# define __raw_ioswabw(a, x)	(x)
# define __raw_ioswabl(a, x)	(x)
# define __raw_ioswabq(a, x)	(x)
# define ____raw_ioswabq(a, x)	(x)

/* ioswab[bwlq], __mem_ioswab[bwlq] are defined in mangle-port.h */

#define IO_SPACE_LIMIT 0xffff

/*
 * On MIPS I/O ports are memory mapped, so we access them using normal
 * load/store instructions. mips_io_port_base is the virtual address to
 * which all ports are being mapped.  For sake of efficiency some code
 * assumes that this is an address that can be loaded with a single lui
 * instruction, so the lower 16 bits must be zero.  Should be true on
 * on any sane architecture; generic code does not use this assumption.
 */
extern const unsigned long mips_io_port_base;

/*
 * Gcc will generate code to load the value of mips_io_port_base after each
 * function call which may be fairly wasteful in some cases.  So we don't
 * play quite by the book.  We tell gcc mips_io_port_base is a long variable
 * which solves the code generation issue.  Now we need to violate the
 * aliasing rules a little to make initialization possible and finally we
 * will need the barrier() to fight side effects of the aliasing chat.
 * This trickery will eventually collapse under gcc's optimizer.  Oh well.
 */
static inline void set_io_port_base(unsigned long base)
{
	* (unsigned long *) &mips_io_port_base = base;
	barrier();
}

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *		Linus
 *
 */

#define __SLOW_DOWN_IO \
	__asm__ __volatile__( \
		"sb\t$0,0x80(%0)" \
		: : "r" (mips_io_port_base));

#ifdef CONF_SLOWDOWN_IO
#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif
#else
#define SLOW_DOWN_IO
#endif

/*
 *     virt_to_phys    -       map virtual addresses to physical
 *     @address: address to remap
 *
 *     The returned physical address is the physical (CPU) mapping for
 *     the memory address given. It is only valid to use this function on
 *     addresses directly mapped or allocated via kmalloc.
 *
 *     This function does not give bus mappings for DMA transfers. In
 *     almost all conceivable cases a device driver should not be using
 *     this function
 */
static inline unsigned long virt_to_phys(volatile const void *address)
{
	return __pa(address);
}

/*
 *     phys_to_virt    -       map physical address to virtual
 *     @address: address to remap
 *
 *     The returned virtual address is a current CPU mapping for
 *     the memory address given. It is only valid to use this function on
 *     addresses that have a kernel mapping
 *
 *     This function does not handle bus mappings for DMA transfers. In
 *     almost all conceivable cases a device driver should not be using
 *     this function
 */
static inline void * phys_to_virt(unsigned long address)
{
	return (void *)(address + PAGE_OFFSET - PHYS_OFFSET);
}

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 */
static inline unsigned long isa_virt_to_bus(volatile void * address)
{
	return (unsigned long)address - PAGE_OFFSET;
}

static inline void * isa_bus_to_virt(unsigned long address)
{
	return (void *)(address + PAGE_OFFSET);
}

#define isa_page_to_bus page_to_phys

/*
 * However PCI ones are not necessarily 1:1 and therefore these interfaces
 * are forbidden in portable PCI drivers.
 *
 * Allow them for x86 for legacy drivers, though.
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

extern void __iomem * __ioremap(phys_addr_t offset, phys_addr_t size, unsigned long flags);
extern void __iounmap(const volatile void __iomem *addr);

#ifndef CONFIG_PCI
struct pci_dev;
static inline void pci_iounmap(struct pci_dev *dev, void __iomem *addr) {}
#endif

static inline void __iomem * __ioremap_mode(phys_addr_t offset, unsigned long size,
	unsigned long flags)
{
	void __iomem *addr = plat_ioremap(offset, size, flags);

	if (addr)
		return addr;

#define __IS_LOW512(addr) (!((phys_addr_t)(addr) & (phys_addr_t) ~0x1fffffffULL))

	if (cpu_has_64bit_addresses) {
		u64 base = UNCAC_BASE;

		/*
		 * R10000 supports a 2 bit uncached attribute therefore
		 * UNCAC_BASE may not equal IO_BASE.
		 */
		if (flags == _CACHE_UNCACHED)
			base = (u64) IO_BASE;
		return (void __iomem *) (unsigned long) (base + offset);
	} else if (__builtin_constant_p(offset) &&
		   __builtin_constant_p(size) && __builtin_constant_p(flags)) {
		phys_addr_t phys_addr, last_addr;

		phys_addr = fixup_bigphys_addr(offset, size);

		/* Don't allow wraparound or zero size. */
		last_addr = phys_addr + size - 1;
		if (!size || last_addr < phys_addr)
			return NULL;

		/*
		 * Map uncached objects in the low 512MB of address
		 * space using KSEG1.
		 */
		if (__IS_LOW512(phys_addr) && __IS_LOW512(last_addr) &&
		    flags == _CACHE_UNCACHED)
			return (void __iomem *)
				(unsigned long)CKSEG1ADDR(phys_addr);
	}

	return __ioremap(offset, size, flags);

#undef __IS_LOW512
}

/*
 * ioremap     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 */
#define ioremap(offset, size)						\
	__ioremap_mode((offset), (size), _CACHE_UNCACHED)

/*
 * ioremap_nocache     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_nocache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * on the CPU as well as honouring existing caching rules from things like
 * the PCI bus. Note that there are other caches and buffers on many
 * busses. In particular driver authors should read up on PCI writes
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 */
#define ioremap_nocache(offset, size)					\
	__ioremap_mode((offset), (size), _CACHE_UNCACHED)
#define ioremap_uc ioremap_nocache

/*
 * ioremap_cachable -	map bus memory into CPU space
 * @offset:	    bus address of the memory
 * @size:	    size of the resource to map
 *
 * ioremap_nocache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked cachable by
 * the CPU.  Also enables full write-combining.	 Useful for some
 * memory-like regions on I/O busses.
 */
#define ioremap_cachable(offset, size)					\
	__ioremap_mode((offset), (size), _page_cachable_default)
#define ioremap_cache ioremap_cachable

/*
 * ioremap_wc     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_wc performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * but accelerated by means of write-combining feature. It is specifically
 * useful for PCIe prefetchable windows, which may vastly improve a
 * communications performance. If it was determined on boot stage, what
 * CPU CCA doesn't support UCA, the method shall fall-back to the
 * _CACHE_UNCACHED option (see cpu_probe() method).
 */
#define ioremap_wc(offset, size)					\
	__ioremap_mode((offset), (size), boot_cpu_data.writecombine)

static inline void iounmap(const volatile void __iomem *addr)
{
	if (plat_iounmap(addr))
		return;

#define __IS_KSEG1(addr) (((unsigned long)(addr) & ~0x1fffffffUL) == CKSEG1)

	if (cpu_has_64bit_addresses ||
	    (__builtin_constant_p(addr) && __IS_KSEG1(addr)))
		return;

	__iounmap(addr);

#undef __IS_KSEG1
}

#if defined(CONFIG_CPU_CAVIUM_OCTEON) || defined(CONFIG_LOONGSON3_ENHANCEMENT)
#define war_io_reorder_wmb()		wmb()
#else
#define war_io_reorder_wmb()		barrier()
#endif

#define __BUILD_MEMORY_SINGLE(pfx, bwlq, type, irq)			\
									\
static inline void pfx##write##bwlq(type val,				\
				    volatile void __iomem *mem)		\
{									\
	volatile type *__mem;						\
	type __val;							\
									\
	war_io_reorder_wmb();					\
									\
	__mem = (void *)__swizzle_addr_##bwlq((unsigned long)(mem));	\
									\
	__val = pfx##ioswab##bwlq(__mem, val);				\
									\
	if (sizeof(type) != sizeof(u64) || sizeof(u64) == sizeof(long)) \
		*__mem = __val;						\
	else if (cpu_has_64bits) {					\
		unsigned long __flags;					\
		type __tmp;						\
									\
		if (irq)						\
			local_irq_save(__flags);			\
		__asm__ __volatile__(					\
			".set	arch=r4000"	"\t\t# __writeq""\n\t"	\
			"dsll32 %L0, %L0, 0"			"\n\t"	\
			"dsrl32 %L0, %L0, 0"			"\n\t"	\
			"dsll32 %M0, %M0, 0"			"\n\t"	\
			"or	%L0, %L0, %M0"			"\n\t"	\
			"sd	%L0, %2"			"\n\t"	\
			".set	mips0"				"\n"	\
			: "=r" (__tmp)					\
			: "0" (__val), "m" (*__mem));			\
		if (irq)						\
			local_irq_restore(__flags);			\
	} else								\
		BUG();							\
}									\
									\
static inline type pfx##read##bwlq(const volatile void __iomem *mem)	\
{									\
	volatile type *__mem;						\
	type __val;							\
									\
	__mem = (void *)__swizzle_addr_##bwlq((unsigned long)(mem));	\
									\
	if (sizeof(type) != sizeof(u64) || sizeof(u64) == sizeof(long)) \
		__val = *__mem;						\
	else if (cpu_has_64bits) {					\
		unsigned long __flags;					\
									\
		if (irq)						\
			local_irq_save(__flags);			\
		__asm__ __volatile__(					\
			".set	arch=r4000"	"\t\t# __readq" "\n\t"	\
			"ld	%L0, %1"			"\n\t"	\
			"dsra32 %M0, %L0, 0"			"\n\t"	\
			"sll	%L0, %L0, 0"			"\n\t"	\
			".set	mips0"				"\n"	\
			: "=r" (__val)					\
			: "m" (*__mem));				\
		if (irq)						\
			local_irq_restore(__flags);			\
	} else {							\
		__val = 0;						\
		BUG();							\
	}								\
									\
	/* prevent prefetching of coherent DMA data prematurely */	\
	rmb();								\
	return pfx##ioswab##bwlq(__mem, __val);				\
}

#define __BUILD_IOPORT_SINGLE(pfx, bwlq, type, p, slow)			\
									\
static inline void pfx##out##bwlq##p(type val, unsigned long port)	\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	war_io_reorder_wmb();					\
									\
	__addr = (void *)__swizzle_addr_##bwlq(mips_io_port_base + port); \
									\
	__val = pfx##ioswab##bwlq(__addr, val);				\
									\
	/* Really, we want this to be atomic */				\
	BUILD_BUG_ON(sizeof(type) > sizeof(unsigned long));		\
									\
	*__addr = __val;						\
	slow;								\
}									\
									\
static inline type pfx##in##bwlq##p(unsigned long port)			\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = (void *)__swizzle_addr_##bwlq(mips_io_port_base + port); \
									\
	BUILD_BUG_ON(sizeof(type) > sizeof(unsigned long));		\
									\
	__val = *__addr;						\
	slow;								\
									\
	/* prevent prefetching of coherent DMA data prematurely */	\
	rmb();								\
	return pfx##ioswab##bwlq(__addr, __val);			\
}

#define __BUILD_MEMORY_PFX(bus, bwlq, type)				\
									\
__BUILD_MEMORY_SINGLE(bus, bwlq, type, 1)

#define BUILDIO_MEM(bwlq, type)						\
									\
__BUILD_MEMORY_PFX(__raw_, bwlq, type)					\
__BUILD_MEMORY_PFX(, bwlq, type)					\
__BUILD_MEMORY_PFX(__mem_, bwlq, type)					\

BUILDIO_MEM(b, u8)
BUILDIO_MEM(w, u16)
BUILDIO_MEM(l, u32)
BUILDIO_MEM(q, u64)

#define __BUILD_IOPORT_PFX(bus, bwlq, type)				\
	__BUILD_IOPORT_SINGLE(bus, bwlq, type, ,)			\
	__BUILD_IOPORT_SINGLE(bus, bwlq, type, _p, SLOW_DOWN_IO)

#define BUILDIO_IOPORT(bwlq, type)					\
	__BUILD_IOPORT_PFX(, bwlq, type)				\
	__BUILD_IOPORT_PFX(__mem_, bwlq, type)

BUILDIO_IOPORT(b, u8)
BUILDIO_IOPORT(w, u16)
BUILDIO_IOPORT(l, u32)
#ifdef CONFIG_64BIT
BUILDIO_IOPORT(q, u64)
#endif

#define __BUILDIO(bwlq, type)						\
									\
__BUILD_MEMORY_SINGLE(____raw_, bwlq, type, 0)

__BUILDIO(q, u64)

#define readb_relaxed			readb
#define readw_relaxed			readw
#define readl_relaxed			readl
#define readq_relaxed			readq

#define writeb_relaxed			writeb
#define writew_relaxed			writew
#define writel_relaxed			writel
#define writeq_relaxed			writeq

#define readb_be(addr)							\
	__raw_readb((__force unsigned *)(addr))
#define readw_be(addr)							\
	be16_to_cpu(__raw_readw((__force unsigned *)(addr)))
#define readl_be(addr)							\
	be32_to_cpu(__raw_readl((__force unsigned *)(addr)))
#define readq_be(addr)							\
	be64_to_cpu(__raw_readq((__force unsigned *)(addr)))

#define writeb_be(val, addr)						\
	__raw_writeb((val), (__force unsigned *)(addr))
#define writew_be(val, addr)						\
	__raw_writew(cpu_to_be16((val)), (__force unsigned *)(addr))
#define writel_be(val, addr)						\
	__raw_writel(cpu_to_be32((val)), (__force unsigned *)(addr))
#define writeq_be(val, addr)						\
	__raw_writeq(cpu_to_be64((val)), (__force unsigned *)(addr))

/*
 * Some code tests for these symbols
 */
#define readq				readq
#define writeq				writeq

#define __BUILD_MEMORY_STRING(bwlq, type)				\
									\
static inline void writes##bwlq(volatile void __iomem *mem,		\
				const void *addr, unsigned int count)	\
{									\
	const volatile type *__addr = addr;				\
									\
	while (count--) {						\
		__mem_write##bwlq(*__addr, mem);			\
		__addr++;						\
	}								\
}									\
									\
static inline void reads##bwlq(volatile void __iomem *mem, void *addr,	\
			       unsigned int count)			\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		*__addr = __mem_read##bwlq(mem);			\
		__addr++;						\
	}								\
}

#define __BUILD_IOPORT_STRING(bwlq, type)				\
									\
static inline void outs##bwlq(unsigned long port, const void *addr,	\
			      unsigned int count)			\
{									\
	const volatile type *__addr = addr;				\
									\
	while (count--) {						\
		__mem_out##bwlq(*__addr, port);				\
		__addr++;						\
	}								\
}									\
									\
static inline void ins##bwlq(unsigned long port, void *addr,		\
			     unsigned int count)			\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		*__addr = __mem_in##bwlq(port);				\
		__addr++;						\
	}								\
}

#define BUILDSTRING(bwlq, type)						\
									\
__BUILD_MEMORY_STRING(bwlq, type)					\
__BUILD_IOPORT_STRING(bwlq, type)

BUILDSTRING(b, u8)
BUILDSTRING(w, u16)
BUILDSTRING(l, u32)
#ifdef CONFIG_64BIT
BUILDSTRING(q, u64)
#endif


#ifdef CONFIG_CPU_CAVIUM_OCTEON
#define mmiowb() wmb()
#else
/* Depends on MIPS II instruction set */
#define mmiowb() asm volatile ("sync" ::: "memory")
#endif

static inline void memset_io(volatile void __iomem *addr, unsigned char val, int count)
{
	memset((void __force *) addr, val, count);
}
static inline void memcpy_fromio(void *dst, const volatile void __iomem *src, int count)
{
	memcpy(dst, (void __force *) src, count);
}
static inline void memcpy_toio(volatile void __iomem *dst, const void *src, int count)
{
	memcpy((void __force *) dst, src, count);
}

/*
 * The caches on some architectures aren't dma-coherent and have need to
 * handle this in software.  There are three types of operations that
 * can be applied to dma buffers.
 *
 *  - dma_cache_wback_inv(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_wback(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 *
 * This API used to be exported; it now is for arch code internal use only.
 */
#ifdef CONFIG_DMA_NONCOHERENT

extern void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
extern void (*_dma_cache_wback)(unsigned long start, unsigned long size);
extern void (*_dma_cache_inv)(unsigned long start, unsigned long size);

#define dma_cache_wback_inv(start, size)	_dma_cache_wback_inv(start, size)
#define dma_cache_wback(start, size)		_dma_cache_wback(start, size)
#define dma_cache_inv(start, size)		_dma_cache_inv(start, size)

#else /* Sane hardware */

#define dma_cache_wback_inv(start,size) \
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_wback(start,size)	\
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_inv(start,size)	\
	do { (void) (start); (void) (size); } while (0)

#endif /* CONFIG_DMA_NONCOHERENT */

/*
 * Read a 32-bit register that requires a 64-bit read cycle on the bus.
 * Avoid interrupt mucking, just adjust the address for 4-byte access.
 * Assume the addresses are 8-byte aligned.
 */
#ifdef __MIPSEB__
#define __CSR_32_ADJUST 4
#else
#define __CSR_32_ADJUST 0
#endif

#define csr_out32(v, a) (*(volatile u32 *)((unsigned long)(a) + __CSR_32_ADJUST) = (v))
#define csr_in32(a)    (*(volatile u32 *)((unsigned long)(a) + __CSR_32_ADJUST))

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

void __ioread64_copy(void *to, const void __iomem *from, size_t count);

#endif /* _ASM_IO_H */
