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
#include <asm/pgtable.h>
#include <asm-generic/iomap.h>

#ifdef __KERNEL__
#define __IO_PREFIX     generic
#include <asm/io_generic.h>
#include <asm/io_trapped.h>
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
static inline void pfx##reads##bwlq(volatile void __iomem *mem,		\
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

#ifdef CONFIG_SUPERH32
void __raw_writesl(void __iomem *addr, const void *data, int longlen);
void __raw_readsl(const void __iomem *addr, void *data, int longlen);
#else
__BUILD_MEMORY_STRING(__raw_, l, u32)
#endif

__BUILD_MEMORY_STRING(__raw_, q, u64)

#ifdef CONFIG_HAS_IOPORT

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
	__addr = __ioport_map(port, sizeof(type));			\
	*__addr = val;							\
	slow;								\
}									\
									\
static inline type pfx##in##bwlq##p(unsigned long port)			\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = __ioport_map(port, sizeof(type));			\
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

#else /* !CONFIG_HAS_IOPORT */

#include <asm/io_noioport.h>

#endif


#define IO_SPACE_LIMIT 0xffffffff

/* synco on SH-4A, otherwise a nop */
#define mmiowb()		wmb()

/* We really want to try and get these to memcpy etc */
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

/*
 * On 32-bit SH, we traditionally have the whole physical address space
 * mapped at all times (as MIPS does), so "ioremap()" and "iounmap()" do
 * not need to do anything but place the address in the proper segment.
 * This is true for P1 and P2 addresses, as well as some P3 ones.
 * However, most of the P3 addresses and newer cores using extended
 * addressing need to map through page tables, so the ioremap()
 * implementation becomes a bit more complicated.
 *
 * See arch/sh/mm/ioremap.c for additional notes on this.
 *
 * We cheat a bit and always return uncachable areas until we've fixed
 * the drivers to handle caching properly.
 *
 * On the SH-5 the concept of segmentation in the 1:1 PXSEG sense simply
 * doesn't exist, so everything must go through page tables.
 */
#ifdef CONFIG_MMU
void __iomem *__ioremap_caller(phys_addr_t offset, unsigned long size,
			       pgprot_t prot, void *caller);
void __iounmap(void __iomem *addr);

static inline void __iomem *
__ioremap(phys_addr_t offset, unsigned long size, pgprot_t prot)
{
	return __ioremap_caller(offset, size, prot, __builtin_return_address(0));
}

static inline void __iomem *
__ioremap_29bit(phys_addr_t offset, unsigned long size, pgprot_t prot)
{
#ifdef CONFIG_29BIT
	phys_addr_t last_addr = offset + size - 1;

	/*
	 * For P1 and P2 space this is trivial, as everything is already
	 * mapped. Uncached access for P1 addresses are done through P2.
	 * In the P3 case or for addresses outside of the 29-bit space,
	 * mapping must be done by the PMB or by using page tables.
	 */
	if (likely(PXSEG(offset) < P3SEG && PXSEG(last_addr) < P3SEG)) {
		u64 flags = pgprot_val(prot);

		/*
		 * Anything using the legacy PTEA space attributes needs
		 * to be kicked down to page table mappings.
		 */
		if (unlikely(flags & _PAGE_PCC_MASK))
			return NULL;
		if (unlikely(flags & _PAGE_CACHABLE))
			return (void __iomem *)P1SEGADDR(offset);

		return (void __iomem *)P2SEGADDR(offset);
	}

	/* P4 above the store queues are always mapped. */
	if (unlikely(offset >= P3_ADDR_MAX))
		return (void __iomem *)P4SEGADDR(offset);
#endif

	return NULL;
}

static inline void __iomem *
__ioremap_mode(phys_addr_t offset, unsigned long size, pgprot_t prot)
{
	void __iomem *ret;

	ret = __ioremap_trapped(offset, size);
	if (ret)
		return ret;

	ret = __ioremap_29bit(offset, size, prot);
	if (ret)
		return ret;

	return __ioremap(offset, size, prot);
}
#else
#define __ioremap(offset, size, prot)		((void __iomem *)(offset))
#define __ioremap_mode(offset, size, prot)	((void __iomem *)(offset))
#define __iounmap(addr)				do { } while (0)
#endif /* CONFIG_MMU */

static inline void __iomem *ioremap(phys_addr_t offset, unsigned long size)
{
	return __ioremap_mode(offset, size, PAGE_KERNEL_NOCACHE);
}

static inline void __iomem *
ioremap_cache(phys_addr_t offset, unsigned long size)
{
	return __ioremap_mode(offset, size, PAGE_KERNEL);
}

#ifdef CONFIG_HAVE_IOREMAP_PROT
static inline void __iomem *
ioremap_prot(phys_addr_t offset, unsigned long size, unsigned long flags)
{
	return __ioremap_mode(offset, size, __pgprot(flags));
}
#endif

#ifdef CONFIG_IOREMAP_FIXED
extern void __iomem *ioremap_fixed(phys_addr_t, unsigned long, pgprot_t);
extern int iounmap_fixed(void __iomem *);
extern void ioremap_fixed_init(void);
#else
static inline void __iomem *
ioremap_fixed(phys_addr_t phys_addr, unsigned long size, pgprot_t prot)
{
	BUG();
	return NULL;
}

static inline void ioremap_fixed_init(void) { }
static inline int iounmap_fixed(void __iomem *addr) { return -EINVAL; }
#endif

#define ioremap_nocache	ioremap
#define iounmap		__iounmap

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#define ARCH_HAS_VALID_PHYS_ADDR_RANGE
int valid_phys_addr_range(unsigned long addr, size_t size);
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size);

#endif /* __KERNEL__ */

#endif /* __ASM_SH_IO_H */
