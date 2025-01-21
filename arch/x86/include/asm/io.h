/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

/*
 * This file contains the definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 */

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *		Linus
 */

 /*
  *  Bit simplified and optimized by Jan Hubicka
  *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999.
  *
  *  isa_memset_io, isa_memcpy_fromio, isa_memcpy_toio added,
  *  isa_read[wl] and isa_write[wl] fixed
  *  - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
  */

#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/cc_platform.h>
#include <asm/page.h>
#include <asm/early_ioremap.h>
#include <asm/pgtable_types.h>
#include <asm/shared/io.h>
#include <asm/special_insns.h>

#define build_mmio_read(name, size, type, reg, barrier) \
static inline type name(const volatile void __iomem *addr) \
{ type ret; asm volatile("mov" size " %1,%0":reg (ret) \
:"m" (*(volatile type __force *)addr) barrier); return ret; }

#define build_mmio_write(name, size, type, reg, barrier) \
static inline void name(type val, volatile void __iomem *addr) \
{ asm volatile("mov" size " %0,%1": :reg (val), \
"m" (*(volatile type __force *)addr) barrier); }

build_mmio_read(readb, "b", unsigned char, "=q", :"memory")
build_mmio_read(readw, "w", unsigned short, "=r", :"memory")
build_mmio_read(readl, "l", unsigned int, "=r", :"memory")

build_mmio_read(__readb, "b", unsigned char, "=q", )
build_mmio_read(__readw, "w", unsigned short, "=r", )
build_mmio_read(__readl, "l", unsigned int, "=r", )

build_mmio_write(writeb, "b", unsigned char, "q", :"memory")
build_mmio_write(writew, "w", unsigned short, "r", :"memory")
build_mmio_write(writel, "l", unsigned int, "r", :"memory")

build_mmio_write(__writeb, "b", unsigned char, "q", )
build_mmio_write(__writew, "w", unsigned short, "r", )
build_mmio_write(__writel, "l", unsigned int, "r", )

#define readb readb
#define readw readw
#define readl readl
#define readb_relaxed(a) __readb(a)
#define readw_relaxed(a) __readw(a)
#define readl_relaxed(a) __readl(a)
#define __raw_readb __readb
#define __raw_readw __readw
#define __raw_readl __readl

#define writeb writeb
#define writew writew
#define writel writel
#define writeb_relaxed(v, a) __writeb(v, a)
#define writew_relaxed(v, a) __writew(v, a)
#define writel_relaxed(v, a) __writel(v, a)
#define __raw_writeb __writeb
#define __raw_writew __writew
#define __raw_writel __writel

#ifdef CONFIG_X86_64

build_mmio_read(readq, "q", u64, "=r", :"memory")
build_mmio_read(__readq, "q", u64, "=r", )
build_mmio_write(writeq, "q", u64, "r", :"memory")
build_mmio_write(__writeq, "q", u64, "r", )

#define readq_relaxed(a)	__readq(a)
#define writeq_relaxed(v, a)	__writeq(v, a)

#define __raw_readq		__readq
#define __raw_writeq		__writeq

/* Let people know that we have them */
#define readq			readq
#define writeq			writeq

#endif

#define ARCH_HAS_VALID_PHYS_ADDR_RANGE
extern int valid_phys_addr_range(phys_addr_t addr, size_t size);
extern int valid_mmap_phys_addr_range(unsigned long pfn, size_t size);

/**
 *	virt_to_phys	-	map virtual addresses to physical
 *	@address: address to remap
 *
 *	The returned physical address is the physical (CPU) mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses directly mapped or allocated via kmalloc.
 *
 *	This function does not give bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */

static inline phys_addr_t virt_to_phys(volatile void *address)
{
	return __pa(address);
}
#define virt_to_phys virt_to_phys

/**
 *	phys_to_virt	-	map physical address to virtual
 *	@address: address to remap
 *
 *	The returned virtual address is a current CPU mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses that have a kernel mapping
 *
 *	This function does not handle bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */

static inline void *phys_to_virt(phys_addr_t address)
{
	return __va(address);
}
#define phys_to_virt phys_to_virt

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 * However, we truncate the address to unsigned int to avoid undesirable
 * promotions in legacy drivers.
 */
static inline unsigned int isa_virt_to_bus(volatile void *address)
{
	return (unsigned int)virt_to_phys(address);
}
#define isa_bus_to_virt		phys_to_virt

/*
 * The default ioremap() behavior is non-cached; if you need something
 * else, you probably want one of the following.
 */
extern void __iomem *ioremap_uc(resource_size_t offset, unsigned long size);
#define ioremap_uc ioremap_uc
extern void __iomem *ioremap_cache(resource_size_t offset, unsigned long size);
#define ioremap_cache ioremap_cache
extern void __iomem *ioremap_prot(resource_size_t offset, unsigned long size, unsigned long prot_val);
#define ioremap_prot ioremap_prot
extern void __iomem *ioremap_encrypted(resource_size_t phys_addr, unsigned long size);
#define ioremap_encrypted ioremap_encrypted

/**
 * ioremap     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * If the area you are trying to map is a PCI BAR you should have a
 * look at pci_iomap().
 */
void __iomem *ioremap(resource_size_t offset, unsigned long size);
#define ioremap ioremap

extern void iounmap(volatile void __iomem *addr);
#define iounmap iounmap

#ifdef __KERNEL__

void memcpy_fromio(void *, const volatile void __iomem *, size_t);
void memcpy_toio(volatile void __iomem *, const void *, size_t);
void memset_io(volatile void __iomem *, int, size_t);

#define memcpy_fromio memcpy_fromio
#define memcpy_toio memcpy_toio
#define memset_io memset_io

#ifdef CONFIG_X86_64
/*
 * Commit 0f07496144c2 ("[PATCH] Add faster __iowrite32_copy routine for
 * x86_64") says that circa 2006 rep movsl is noticeably faster than a copy
 * loop.
 */
static inline void __iowrite32_copy(void __iomem *to, const void *from,
				    size_t count)
{
	asm volatile("rep ; movsl"
		     : "=&c"(count), "=&D"(to), "=&S"(from)
		     : "0"(count), "1"(to), "2"(from)
		     : "memory");
}
#define __iowrite32_copy __iowrite32_copy
#endif

/*
 * ISA space is 'always mapped' on a typical x86 system, no need to
 * explicitly ioremap() it. The fact that the ISA IO space is mapped
 * to PAGE_OFFSET is pure coincidence - it does not mean ISA values
 * are physical addresses. The following constant pointer can be
 * used as the IO-area pointer (it can be iounmapped as well, so the
 * analogy with PCI is quite large):
 */
#define __ISA_IO_base ((char __iomem *)(PAGE_OFFSET))

#endif /* __KERNEL__ */

extern void native_io_delay(void);

extern int io_delay_type;
extern void io_delay_init(void);

#if defined(CONFIG_PARAVIRT)
#include <asm/paravirt.h>
#else

static inline void slow_down_io(void)
{
	native_io_delay();
#ifdef REALLY_SLOW_IO
	native_io_delay();
	native_io_delay();
	native_io_delay();
#endif
}

#endif

#define BUILDIO(bwl, type)						\
static inline void out##bwl##_p(type value, u16 port)			\
{									\
	out##bwl(value, port);						\
	slow_down_io();							\
}									\
									\
static inline type in##bwl##_p(u16 port)				\
{									\
	type value = in##bwl(port);					\
	slow_down_io();							\
	return value;							\
}									\
									\
static inline void outs##bwl(u16 port, const void *addr, unsigned long count) \
{									\
	if (cc_platform_has(CC_ATTR_GUEST_UNROLL_STRING_IO)) {		\
		type *value = (type *)addr;				\
		while (count) {						\
			out##bwl(*value, port);				\
			value++;					\
			count--;					\
		}							\
	} else {							\
		asm volatile("rep; outs" #bwl				\
			     : "+S"(addr), "+c"(count)			\
			     : "d"(port) : "memory");			\
	}								\
}									\
									\
static inline void ins##bwl(u16 port, void *addr, unsigned long count)	\
{									\
	if (cc_platform_has(CC_ATTR_GUEST_UNROLL_STRING_IO)) {		\
		type *value = (type *)addr;				\
		while (count) {						\
			*value = in##bwl(port);				\
			value++;					\
			count--;					\
		}							\
	} else {							\
		asm volatile("rep; ins" #bwl				\
			     : "+D"(addr), "+c"(count)			\
			     : "d"(port) : "memory");			\
	}								\
}

BUILDIO(b, u8)
BUILDIO(w, u16)
BUILDIO(l, u32)
#undef BUILDIO

#define inb_p inb_p
#define inw_p inw_p
#define inl_p inl_p
#define insb insb
#define insw insw
#define insl insl

#define outb_p outb_p
#define outw_p outw_p
#define outl_p outl_p
#define outsb outsb
#define outsw outsw
#define outsl outsl

extern void *xlate_dev_mem_ptr(phys_addr_t phys);
extern void unxlate_dev_mem_ptr(phys_addr_t phys, void *addr);

#define xlate_dev_mem_ptr xlate_dev_mem_ptr
#define unxlate_dev_mem_ptr unxlate_dev_mem_ptr

extern int ioremap_change_attr(unsigned long vaddr, unsigned long size,
				enum page_cache_mode pcm);
extern void __iomem *ioremap_wc(resource_size_t offset, unsigned long size);
#define ioremap_wc ioremap_wc
extern void __iomem *ioremap_wt(resource_size_t offset, unsigned long size);
#define ioremap_wt ioremap_wt

extern bool is_early_ioremap_ptep(pte_t *ptep);

#define IO_SPACE_LIMIT 0xffff

#include <asm-generic/io.h>
#undef PCI_IOBASE

#ifdef CONFIG_MTRR
extern int __must_check arch_phys_wc_index(int handle);
#define arch_phys_wc_index arch_phys_wc_index

extern int __must_check arch_phys_wc_add(unsigned long base,
					 unsigned long size);
extern void arch_phys_wc_del(int handle);
#define arch_phys_wc_add arch_phys_wc_add
#endif

#ifdef CONFIG_X86_PAT
extern int arch_io_reserve_memtype_wc(resource_size_t start, resource_size_t size);
extern void arch_io_free_memtype_wc(resource_size_t start, resource_size_t size);
#define arch_io_reserve_memtype_wc arch_io_reserve_memtype_wc
#endif

#ifdef CONFIG_AMD_MEM_ENCRYPT
extern bool arch_memremap_can_ram_remap(resource_size_t offset,
					unsigned long size,
					unsigned long flags);
#define arch_memremap_can_ram_remap arch_memremap_can_ram_remap

extern bool phys_mem_access_encrypted(unsigned long phys_addr,
				      unsigned long size);
#else
static inline bool phys_mem_access_encrypted(unsigned long phys_addr,
					     unsigned long size)
{
	return true;
}
#endif

/**
 * iosubmit_cmds512 - copy data to single MMIO location, in 512-bit units
 * @dst: destination, in MMIO space (must be 512-bit aligned)
 * @src: source
 * @count: number of 512 bits quantities to submit
 *
 * Submit data from kernel space to MMIO space, in units of 512 bits at a
 * time.  Order of access is not guaranteed, nor is a memory barrier
 * performed afterwards.
 *
 * Warning: Do not use this helper unless your driver has checked that the CPU
 * instruction is supported on the platform.
 */
static inline void iosubmit_cmds512(void __iomem *dst, const void *src,
				    size_t count)
{
	const u8 *from = src;
	const u8 *end = from + count * 64;

	while (from < end) {
		movdir64b_io(dst, from);
		from += 64;
	}
}

#endif /* _ASM_X86_IO_H */
