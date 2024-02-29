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

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/irqflags.h>

#include <asm/addrspace.h>
#include <asm/barrier.h>
#include <asm/bug.h>
#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/string.h>
#include <mangle-port.h>

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

# define _ioswabb ioswabb
# define _ioswabw ioswabw
# define _ioswabl ioswabl
# define _ioswabq ioswabq

# define __relaxed_ioswabb ioswabb
# define __relaxed_ioswabw ioswabw
# define __relaxed_ioswabl ioswabl
# define __relaxed_ioswabq ioswabq

/* ioswab[bwlq], __mem_ioswab[bwlq] are defined in mangle-port.h */

/*
 * On MIPS I/O ports are memory mapped, so we access them using normal
 * load/store instructions. mips_io_port_base is the virtual address to
 * which all ports are being mapped.  For sake of efficiency some code
 * assumes that this is an address that can be loaded with a single lui
 * instruction, so the lower 16 bits must be zero.  Should be true on
 * any sane architecture; generic code does not use this assumption.
 */
extern unsigned long mips_io_port_base;

static inline void set_io_port_base(unsigned long base)
{
	mips_io_port_base = base;
}

/*
 * Provide the necessary definitions for generic iomap. We make use of
 * mips_io_port_base for iomap(), but we don't reserve any low addresses for
 * use with I/O ports.
 */

#define HAVE_ARCH_PIO_SIZE
#define PIO_OFFSET	mips_io_port_base
#define PIO_MASK	IO_SPACE_LIMIT
#define PIO_RESERVED	0x0UL

/*
 * Enforce in-order execution of data I/O.  In the MIPS architecture
 * these are equivalent to corresponding platform-specific memory
 * barriers defined in <asm/barrier.h>.  API pinched from PowerPC,
 * with sync additionally defined.
 */
#define iobarrier_rw() mb()
#define iobarrier_r() rmb()
#define iobarrier_w() wmb()
#define iobarrier_sync() iob()

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
static inline unsigned long __virt_to_phys_nodebug(volatile const void *address)
{
	return __pa(address);
}

#ifdef CONFIG_DEBUG_VIRTUAL
extern phys_addr_t __virt_to_phys(volatile const void *x);
#else
#define __virt_to_phys(x)	__virt_to_phys_nodebug(x)
#endif

#define virt_to_phys virt_to_phys
static inline phys_addr_t virt_to_phys(const volatile void *x)
{
	return __virt_to_phys(x);
}

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 */
static inline unsigned long isa_virt_to_bus(volatile void *address)
{
	return virt_to_phys(address);
}

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

void __iomem *ioremap_prot(phys_addr_t offset, unsigned long size,
		unsigned long prot_val);
void iounmap(const volatile void __iomem *addr);

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
	ioremap_prot((offset), (size), _CACHE_UNCACHED)

/*
 * ioremap_cache -	map bus memory into CPU space
 * @offset:	    bus address of the memory
 * @size:	    size of the resource to map
 *
 * ioremap_cache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked cacheable by
 * the CPU.  Also enables full write-combining.	 Useful for some
 * memory-like regions on I/O busses.
 */
#define ioremap_cache(offset, size)					\
	ioremap_prot((offset), (size), _page_cachable_default)

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
 * This version of ioremap ensures that the memory is marked uncacheable
 * but accelerated by means of write-combining feature. It is specifically
 * useful for PCIe prefetchable windows, which may vastly improve a
 * communications performance. If it was determined on boot stage, what
 * CPU CCA doesn't support UCA, the method shall fall-back to the
 * _CACHE_UNCACHED option (see cpu_probe() method).
 */
#define ioremap_wc(offset, size)					\
	ioremap_prot((offset), (size), boot_cpu_data.writecombine)

#if defined(CONFIG_CPU_CAVIUM_OCTEON)
#define war_io_reorder_wmb()		wmb()
#else
#define war_io_reorder_wmb()		barrier()
#endif

#define __BUILD_MEMORY_SINGLE(pfx, bwlq, type, barrier, relax, irq)	\
									\
static inline void pfx##write##bwlq(type val,				\
				    volatile void __iomem *mem)		\
{									\
	volatile type *__mem;						\
	type __val;							\
									\
	if (barrier)							\
		iobarrier_rw();						\
	else								\
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
			".set	push"		"\t\t# __writeq""\n\t"	\
			".set	arch=r4000"			"\n\t"	\
			"dsll32 %L0, %L0, 0"			"\n\t"	\
			"dsrl32 %L0, %L0, 0"			"\n\t"	\
			"dsll32 %M0, %M0, 0"			"\n\t"	\
			"or	%L0, %L0, %M0"			"\n\t"	\
			"sd	%L0, %2"			"\n\t"	\
			".set	pop"				"\n"	\
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
	if (barrier)							\
		iobarrier_rw();						\
									\
	if (sizeof(type) != sizeof(u64) || sizeof(u64) == sizeof(long)) \
		__val = *__mem;						\
	else if (cpu_has_64bits) {					\
		unsigned long __flags;					\
									\
		if (irq)						\
			local_irq_save(__flags);			\
		__asm__ __volatile__(					\
			".set	push"		"\t\t# __readq" "\n\t"	\
			".set	arch=r4000"			"\n\t"	\
			"ld	%L0, %1"			"\n\t"	\
			"dsra32 %M0, %L0, 0"			"\n\t"	\
			"sll	%L0, %L0, 0"			"\n\t"	\
			".set	pop"				"\n"	\
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
	if (!relax)							\
		rmb();							\
	return pfx##ioswab##bwlq(__mem, __val);				\
}

#define __BUILD_IOPORT_SINGLE(pfx, bwlq, type, barrier, relax)		\
									\
static inline void pfx##out##bwlq(type val, unsigned long port)		\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	if (barrier)							\
		iobarrier_rw();						\
	else								\
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
}									\
									\
static inline type pfx##in##bwlq(unsigned long port)			\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = (void *)__swizzle_addr_##bwlq(mips_io_port_base + port); \
									\
	BUILD_BUG_ON(sizeof(type) > sizeof(unsigned long));		\
									\
	if (barrier)							\
		iobarrier_rw();						\
									\
	__val = *__addr;						\
									\
	/* prevent prefetching of coherent DMA data prematurely */	\
	if (!relax)							\
		rmb();							\
	return pfx##ioswab##bwlq(__addr, __val);			\
}

#define __BUILD_MEMORY_PFX(bus, bwlq, type, relax)			\
									\
__BUILD_MEMORY_SINGLE(bus, bwlq, type, 1, relax, 1)

#define BUILDIO_MEM(bwlq, type)						\
									\
__BUILD_MEMORY_PFX(__raw_, bwlq, type, 0)				\
__BUILD_MEMORY_PFX(__relaxed_, bwlq, type, 1)				\
__BUILD_MEMORY_PFX(__mem_, bwlq, type, 0)				\
__BUILD_MEMORY_PFX(, bwlq, type, 0)

BUILDIO_MEM(b, u8)
BUILDIO_MEM(w, u16)
BUILDIO_MEM(l, u32)
#ifdef CONFIG_64BIT
BUILDIO_MEM(q, u64)
#else
__BUILD_MEMORY_PFX(__raw_, q, u64, 0)
__BUILD_MEMORY_PFX(__mem_, q, u64, 0)
#endif

#define __BUILD_IOPORT_PFX(bus, bwlq, type)				\
	__BUILD_IOPORT_SINGLE(bus, bwlq, type, 1, 0)

#define BUILDIO_IOPORT(bwlq, type)					\
	__BUILD_IOPORT_PFX(_, bwlq, type)				\
	__BUILD_IOPORT_PFX(__mem_, bwlq, type)

BUILDIO_IOPORT(b, u8)
BUILDIO_IOPORT(w, u16)
BUILDIO_IOPORT(l, u32)
#ifdef CONFIG_64BIT
BUILDIO_IOPORT(q, u64)
#endif

#define __BUILDIO(bwlq, type)						\
									\
__BUILD_MEMORY_SINGLE(____raw_, bwlq, type, 1, 0, 0)

__BUILDIO(q, u64)

#define readb_relaxed			__relaxed_readb
#define readw_relaxed			__relaxed_readw
#define readl_relaxed			__relaxed_readl
#ifdef CONFIG_64BIT
#define readq_relaxed			__relaxed_readq
#endif

#define writeb_relaxed			__relaxed_writeb
#define writew_relaxed			__relaxed_writew
#define writel_relaxed			__relaxed_writel
#ifdef CONFIG_64BIT
#define writeq_relaxed			__relaxed_writeq
#endif

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

#define __raw_readb __raw_readb
#define __raw_readw __raw_readw
#define __raw_readl __raw_readl
#ifdef CONFIG_64BIT
#define __raw_readq __raw_readq
#endif
#define __raw_writeb __raw_writeb
#define __raw_writew __raw_writew
#define __raw_writel __raw_writel
#ifdef CONFIG_64BIT
#define __raw_writeq __raw_writeq
#endif

#define readb readb
#define readw readw
#define readl readl
#ifdef CONFIG_64BIT
#define readq readq
#endif
#define writeb writeb
#define writew writew
#define writel writel
#ifdef CONFIG_64BIT
#define writeq writeq
#endif

#define readsb readsb
#define readsw readsw
#define readsl readsl
#ifdef CONFIG_64BIT
#define readsq readsq
#endif
#define writesb writesb
#define writesw writesw
#define writesl writesl
#ifdef CONFIG_64BIT
#define writesq writesq
#endif

#define _inb _inb
#define _inw _inw
#define _inl _inl
#define insb insb
#define insw insw
#define insl insl

#define _outb _outb
#define _outw _outw
#define _outl _outl
#define outsb outsb
#define outsw outsw
#define outsl outsl

void __ioread64_copy(void *to, const void __iomem *from, size_t count);

#include <asm-generic/io.h>

static inline void *isa_bus_to_virt(unsigned long address)
{
	return phys_to_virt(address);
}

#endif /* _ASM_IO_H */
