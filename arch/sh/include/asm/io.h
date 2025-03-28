/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_IO_H
#define __ASM_SH_IO_H

/*
 * Convention:
 *    read{b,w,l,q}/write{b,w,l,q} are for PCI,
 *    while in{b,w,l}/out{b,w,l} are for ISA
 *
 * In addition we have 'pausing' versions: in{b,w,l}_p/out{b,w,l}_p
 * and 'string' versions: ins{b,w,l}/outs{b,w,l}
 *
 * While read{b,w,l,q} and write{b,w,l,q} contain memory barriers
 * automatically, there are also __raw versions, which do not.
 */
#include <linux/errno.h>
#include <asm/cache.h>
#include <asm/addrspace.h>
#include <asm/machvec.h>
#include <asm/page.h>
#include <linux/pgtable.h>

#define __IO_PREFIX     generic
#include <asm/io_generic.h>
#include <asm-generic/pci_iomap.h>
#include <mach/mangle-port.h>

#define __raw_writeb(v,a)	(__chk_io_ptr(a), *(volatile u8  __force *)(a) = (v))
#define __raw_writew(v,a)	(__chk_io_ptr(a), *(volatile u16 __force *)(a) = (v))
#define __raw_writel(v,a)	(__chk_io_ptr(a), *(volatile u32 __force *)(a) = (v))
#define __raw_writeq(v,a)	(__chk_io_ptr(a), *(volatile u64 __force *)(a) = (v))

#define __raw_readb(a)		(__chk_io_ptr(a), *(volatile u8  __force *)(a))
#define __raw_readw(a)		(__chk_io_ptr(a), *(volatile u16 __force *)(a))
#define __raw_readl(a)		(__chk_io_ptr(a), *(volatile u32 __force *)(a))
#define __raw_readq(a)		(__chk_io_ptr(a), *(volatile u64 __force *)(a))

#define readb_relaxed(c)	({ u8  __v = ioswabb(__raw_readb(c)); __v; })
#define readw_relaxed(c)	({ u16 __v = ioswabw(__raw_readw(c)); __v; })
#define readl_relaxed(c)	({ u32 __v = ioswabl(__raw_readl(c)); __v; })
#define readq_relaxed(c)	({ u64 __v = ioswabq(__raw_readq(c)); __v; })

#define writeb_relaxed(v,c)	((void)__raw_writeb((__force  u8)ioswabb(v),c))
#define writew_relaxed(v,c)	((void)__raw_writew((__force u16)ioswabw(v),c))
#define writel_relaxed(v,c)	((void)__raw_writel((__force u32)ioswabl(v),c))
#define writeq_relaxed(v,c)	((void)__raw_writeq((__force u64)ioswabq(v),c))

#define readb(a)		({ u8  r_ = readb_relaxed(a); rmb(); r_; })
#define readw(a)		({ u16 r_ = readw_relaxed(a); rmb(); r_; })
#define readl(a)		({ u32 r_ = readl_relaxed(a); rmb(); r_; })
#define readq(a)		({ u64 r_ = readq_relaxed(a); rmb(); r_; })

#define writeb(v,a)		({ wmb(); writeb_relaxed((v),(a)); })
#define writew(v,a)		({ wmb(); writew_relaxed((v),(a)); })
#define writel(v,a)		({ wmb(); writel_relaxed((v),(a)); })
#define writeq(v,a)		({ wmb(); writeq_relaxed((v),(a)); })

#define readsb(p,d,l)		__raw_readsb(p,d,l)
#define readsw(p,d,l)		__raw_readsw(p,d,l)
#define readsl(p,d,l)		__raw_readsl(p,d,l)

#define writesb(p,d,l)		__raw_writesb(p,d,l)
#define writesw(p,d,l)		__raw_writesw(p,d,l)
#define writesl(p,d,l)		__raw_writesl(p,d,l)

#define __BUILD_UNCACHED_IO(bwlq, type)					\
static inline type read##bwlq##_uncached(unsigned long addr)		\
{									\
	type ret;							\
	jump_to_uncached();						\
	ret = __raw_read##bwlq(addr);					\
	back_to_cached();						\
	return ret;							\
}									\
									\
static inline void write##bwlq##_uncached(type v, unsigned long addr)	\
{									\
	jump_to_uncached();						\
	__raw_write##bwlq(v, addr);					\
	back_to_cached();						\
}

__BUILD_UNCACHED_IO(b, u8)
__BUILD_UNCACHED_IO(w, u16)
__BUILD_UNCACHED_IO(l, u32)
__BUILD_UNCACHED_IO(q, u64)

#define __BUILD_MEMORY_STRING(pfx, bwlq, type)				\
									\
static inline void							\
pfx##writes##bwlq(volatile void __iomem *mem, const void *addr,		\
		  unsigned int count)					\
{									\
	const volatile type *__addr = addr;				\
									\
	while (count--) {						\
		__raw_write##bwlq(*__addr, mem);			\
		__addr++;						\
	}								\
}									\
									\
static inline void pfx##reads##bwlq(const volatile void __iomem *mem,	\
				    void *addr, unsigned int count)	\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		*__addr = __raw_read##bwlq(mem);			\
		__addr++;						\
	}								\
}

__BUILD_MEMORY_STRING(__raw_, b, u8)
__BUILD_MEMORY_STRING(__raw_, w, u16)

void __raw_writesl(void volatile __iomem *addr, const void *data, int longlen);
void __raw_readsl(const volatile void __iomem *addr, void *data, int longlen);

__BUILD_MEMORY_STRING(__raw_, q, u64)

#define ioport_map ioport_map
#define pci_iounmap pci_iounmap

#ifdef CONFIG_HAS_IOPORT_MAP

extern void __iomem *ioport_map(unsigned long port, unsigned int nr);

/*
 * Slowdown I/O port space accesses for antique hardware.
 */
#undef CONF_SLOWDOWN_IO

/*
 * On SuperH I/O ports are memory mapped, so we access them using normal
 * load/store instructions. sh_io_port_base is the virtual address to
 * which all ports are being mapped.
 */
extern unsigned long sh_io_port_base;

static inline void __set_io_port_base(unsigned long pbase)
{
	*(unsigned long *)&sh_io_port_base = pbase;
	barrier();
}

#ifdef CONFIG_GENERIC_IOMAP
#define __ioport_map ioport_map
#else
extern void __iomem *__ioport_map(unsigned long addr, unsigned int size);
#endif

#ifdef CONF_SLOWDOWN_IO
#define SLOW_DOWN_IO __raw_readw(sh_io_port_base)
#else
#define SLOW_DOWN_IO
#endif

#define __BUILD_IOPORT_SINGLE(pfx, bwlq, type, p, slow)			\
									\
static inline void pfx##out##bwlq##p(type val, unsigned long port)	\
{									\
	volatile type *__addr;						\
									\
	__addr = (void __iomem *)sh_io_port_base + port;		\
	*__addr = val;							\
	slow;								\
}									\
									\
static inline type pfx##in##bwlq##p(unsigned long port)			\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = (void __iomem *)sh_io_port_base + port;		\
	__val = *__addr;						\
	slow;								\
									\
	return __val;							\
}

#define __BUILD_IOPORT_PFX(bus, bwlq, type)				\
	__BUILD_IOPORT_SINGLE(bus, bwlq, type, ,)			\
	__BUILD_IOPORT_SINGLE(bus, bwlq, type, _p, SLOW_DOWN_IO)

#define BUILDIO_IOPORT(bwlq, type)					\
	__BUILD_IOPORT_PFX(, bwlq, type)

BUILDIO_IOPORT(b, u8)
BUILDIO_IOPORT(w, u16)
BUILDIO_IOPORT(l, u32)
BUILDIO_IOPORT(q, u64)

#define __BUILD_IOPORT_STRING(bwlq, type)				\
									\
static inline void outs##bwlq(unsigned long port, const void *addr,	\
			      unsigned int count)			\
{									\
	const volatile type *__addr = addr;				\
									\
	while (count--) {						\
		out##bwlq(*__addr, port);				\
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
		*__addr = in##bwlq(port);				\
		__addr++;						\
	}								\
}

__BUILD_IOPORT_STRING(b, u8)
__BUILD_IOPORT_STRING(w, u16)
__BUILD_IOPORT_STRING(l, u32)
__BUILD_IOPORT_STRING(q, u64)

#else /* !CONFIG_HAS_IOPORT_MAP */

#include <asm/io_noioport.h>

#endif

#define inb(addr)      inb(addr)
#define inw(addr)      inw(addr)
#define inl(addr)      inl(addr)
#define outb(x, addr)  outb((x), (addr))
#define outw(x, addr)  outw((x), (addr))
#define outl(x, addr)  outl((x), (addr))

#define inb_p(addr)    inb(addr)
#define inw_p(addr)    inw(addr)
#define inl_p(addr)    inl(addr)
#define outb_p(x, addr)        outb((x), (addr))
#define outw_p(x, addr)        outw((x), (addr))
#define outl_p(x, addr)        outl((x), (addr))

#define insb insb
#define insw insw
#define insl insl
#define outsb outsb
#define outsw outsw
#define outsl outsl

#define IO_SPACE_LIMIT 0xffffffff

/* We really want to try and get these to memcpy etc */
#define memset_io memset_io
#define memcpy_fromio memcpy_fromio
#define memcpy_toio memcpy_toio
void memcpy_fromio(void *, const volatile void __iomem *, unsigned long);
void memcpy_toio(volatile void __iomem *, const void *, unsigned long);
void memset_io(volatile void __iomem *, int, unsigned long);

/* Quad-word real-mode I/O, don't ask.. */
unsigned long long peek_real_address_q(unsigned long long addr);
unsigned long long poke_real_address_q(unsigned long long addr,
				       unsigned long long val);

#if !defined(CONFIG_MMU)
#define virt_to_phys(address)	((unsigned long)(address))
#define phys_to_virt(address)	((void *)(address))
#else
#define virt_to_phys(address)	(__pa(address))
#define phys_to_virt(address)	(__va(address))
#endif

#ifdef CONFIG_MMU
/*
 * I/O memory mapping functions.
 */
#define ioremap_prot ioremap_prot
#define iounmap iounmap

#define _PAGE_IOREMAP pgprot_val(PAGE_KERNEL_NOCACHE)

#define ioremap_cache(addr, size)  \
	ioremap_prot((addr), (size), pgprot_val(PAGE_KERNEL))
#endif /* CONFIG_MMU */

#include <asm-generic/io.h>

#define ARCH_HAS_VALID_PHYS_ADDR_RANGE
int valid_phys_addr_range(phys_addr_t addr, size_t size);
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size);

#endif /* __ASM_SH_IO_H */
