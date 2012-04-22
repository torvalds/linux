#ifndef __ALPHA_IO_H
#define __ALPHA_IO_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/compiler.h>
#include <asm/pgtable.h>
#include <asm/machvec.h>
#include <asm/hwrpb.h>

/* The generic header contains only prototypes.  Including it ensures that
   the implementation we have here matches that interface.  */
#include <asm-generic/iomap.h>

/* We don't use IO slowdowns on the Alpha, but.. */
#define __SLOW_DOWN_IO	do { } while (0)
#define SLOW_DOWN_IO	do { } while (0)

/*
 * Virtual -> physical identity mapping starts at this offset
 */
#ifdef USE_48_BIT_KSEG
#define IDENT_ADDR     0xffff800000000000UL
#else
#define IDENT_ADDR     0xfffffc0000000000UL
#endif

/*
 * We try to avoid hae updates (thus the cache), but when we
 * do need to update the hae, we need to do it atomically, so
 * that any interrupts wouldn't get confused with the hae
 * register not being up-to-date with respect to the hardware
 * value.
 */
extern inline void __set_hae(unsigned long new_hae)
{
	unsigned long flags = swpipl(IPL_MAX);

	barrier();

	alpha_mv.hae_cache = new_hae;
	*alpha_mv.hae_register = new_hae;
	mb();
	/* Re-read to make sure it was written.  */
	new_hae = *alpha_mv.hae_register;

	setipl(flags);
	barrier();
}

extern inline void set_hae(unsigned long new_hae)
{
	if (new_hae != alpha_mv.hae_cache)
		__set_hae(new_hae);
}

/*
 * Change virtual addresses to physical addresses and vv.
 */
#ifdef USE_48_BIT_KSEG
static inline unsigned long virt_to_phys(void *address)
{
	return (unsigned long)address - IDENT_ADDR;
}

static inline void * phys_to_virt(unsigned long address)
{
	return (void *) (address + IDENT_ADDR);
}
#else
static inline unsigned long virt_to_phys(void *address)
{
        unsigned long phys = (unsigned long)address;

	/* Sign-extend from bit 41.  */
	phys <<= (64 - 41);
	phys = (long)phys >> (64 - 41);

	/* Crop to the physical address width of the processor.  */
        phys &= (1ul << hwrpb->pa_bits) - 1;

        return phys;
}

static inline void * phys_to_virt(unsigned long address)
{
        return (void *)(IDENT_ADDR + (address & ((1ul << 41) - 1)));
}
#endif

#define page_to_phys(page)	page_to_pa(page)

static inline dma_addr_t __deprecated isa_page_to_bus(struct page *page)
{
	return page_to_phys(page);
}

/* Maximum PIO space address supported?  */
#define IO_SPACE_LIMIT 0xffff

/*
 * Change addresses as seen by the kernel (virtual) to addresses as
 * seen by a device (bus), and vice versa.
 *
 * Note that this only works for a limited range of kernel addresses,
 * and very well may not span all memory.  Consider this interface 
 * deprecated in favour of the DMA-mapping API.
 */
extern unsigned long __direct_map_base;
extern unsigned long __direct_map_size;

static inline unsigned long __deprecated virt_to_bus(void *address)
{
	unsigned long phys = virt_to_phys(address);
	unsigned long bus = phys + __direct_map_base;
	return phys <= __direct_map_size ? bus : 0;
}
#define isa_virt_to_bus virt_to_bus

static inline void * __deprecated bus_to_virt(unsigned long address)
{
	void *virt;

	/* This check is a sanity check but also ensures that bus address 0
	   maps to virtual address 0 which is useful to detect null pointers
	   (the NCR driver is much simpler if NULL pointers are preserved).  */
	address -= __direct_map_base;
	virt = phys_to_virt(address);
	return (long)address <= 0 ? NULL : virt;
}
#define isa_bus_to_virt bus_to_virt

/*
 * There are different chipsets to interface the Alpha CPUs to the world.
 */

#define IO_CONCAT(a,b)	_IO_CONCAT(a,b)
#define _IO_CONCAT(a,b)	a ## _ ## b

#ifdef CONFIG_ALPHA_GENERIC

/* In a generic kernel, we always go through the machine vector.  */

#define REMAP1(TYPE, NAME, QUAL)					\
static inline TYPE generic_##NAME(QUAL void __iomem *addr)		\
{									\
	return alpha_mv.mv_##NAME(addr);				\
}

#define REMAP2(TYPE, NAME, QUAL)					\
static inline void generic_##NAME(TYPE b, QUAL void __iomem *addr)	\
{									\
	alpha_mv.mv_##NAME(b, addr);					\
}

REMAP1(unsigned int, ioread8, /**/)
REMAP1(unsigned int, ioread16, /**/)
REMAP1(unsigned int, ioread32, /**/)
REMAP1(u8, readb, const volatile)
REMAP1(u16, readw, const volatile)
REMAP1(u32, readl, const volatile)
REMAP1(u64, readq, const volatile)

REMAP2(u8, iowrite8, /**/)
REMAP2(u16, iowrite16, /**/)
REMAP2(u32, iowrite32, /**/)
REMAP2(u8, writeb, volatile)
REMAP2(u16, writew, volatile)
REMAP2(u32, writel, volatile)
REMAP2(u64, writeq, volatile)

#undef REMAP1
#undef REMAP2

extern inline void __iomem *generic_ioportmap(unsigned long a)
{
	return alpha_mv.mv_ioportmap(a);
}

static inline void __iomem *generic_ioremap(unsigned long a, unsigned long s)
{
	return alpha_mv.mv_ioremap(a, s);
}

static inline void generic_iounmap(volatile void __iomem *a)
{
	return alpha_mv.mv_iounmap(a);
}

static inline int generic_is_ioaddr(unsigned long a)
{
	return alpha_mv.mv_is_ioaddr(a);
}

static inline int generic_is_mmio(const volatile void __iomem *a)
{
	return alpha_mv.mv_is_mmio(a);
}

#define __IO_PREFIX		generic
#define generic_trivial_rw_bw	0
#define generic_trivial_rw_lq	0
#define generic_trivial_io_bw	0
#define generic_trivial_io_lq	0
#define generic_trivial_iounmap	0

#else

#if defined(CONFIG_ALPHA_APECS)
# include <asm/core_apecs.h>
#elif defined(CONFIG_ALPHA_CIA)
# include <asm/core_cia.h>
#elif defined(CONFIG_ALPHA_IRONGATE)
# include <asm/core_irongate.h>
#elif defined(CONFIG_ALPHA_JENSEN)
# include <asm/jensen.h>
#elif defined(CONFIG_ALPHA_LCA)
# include <asm/core_lca.h>
#elif defined(CONFIG_ALPHA_MARVEL)
# include <asm/core_marvel.h>
#elif defined(CONFIG_ALPHA_MCPCIA)
# include <asm/core_mcpcia.h>
#elif defined(CONFIG_ALPHA_POLARIS)
# include <asm/core_polaris.h>
#elif defined(CONFIG_ALPHA_T2)
# include <asm/core_t2.h>
#elif defined(CONFIG_ALPHA_TSUNAMI)
# include <asm/core_tsunami.h>
#elif defined(CONFIG_ALPHA_TITAN)
# include <asm/core_titan.h>
#elif defined(CONFIG_ALPHA_WILDFIRE)
# include <asm/core_wildfire.h>
#else
#error "What system is this?"
#endif

#endif /* GENERIC */

/*
 * We always have external versions of these routines.
 */
extern u8		inb(unsigned long port);
extern u16		inw(unsigned long port);
extern u32		inl(unsigned long port);
extern void		outb(u8 b, unsigned long port);
extern void		outw(u16 b, unsigned long port);
extern void		outl(u32 b, unsigned long port);

extern u8		readb(const volatile void __iomem *addr);
extern u16		readw(const volatile void __iomem *addr);
extern u32		readl(const volatile void __iomem *addr);
extern u64		readq(const volatile void __iomem *addr);
extern void		writeb(u8 b, volatile void __iomem *addr);
extern void		writew(u16 b, volatile void __iomem *addr);
extern void		writel(u32 b, volatile void __iomem *addr);
extern void		writeq(u64 b, volatile void __iomem *addr);

extern u8		__raw_readb(const volatile void __iomem *addr);
extern u16		__raw_readw(const volatile void __iomem *addr);
extern u32		__raw_readl(const volatile void __iomem *addr);
extern u64		__raw_readq(const volatile void __iomem *addr);
extern void		__raw_writeb(u8 b, volatile void __iomem *addr);
extern void		__raw_writew(u16 b, volatile void __iomem *addr);
extern void		__raw_writel(u32 b, volatile void __iomem *addr);
extern void		__raw_writeq(u64 b, volatile void __iomem *addr);

/*
 * Mapping from port numbers to __iomem space is pretty easy.
 */

/* These two have to be extern inline because of the extern prototype from
   <asm-generic/iomap.h>.  It is not legal to mix "extern" and "static" for
   the same declaration.  */
extern inline void __iomem *ioport_map(unsigned long port, unsigned int size)
{
	return IO_CONCAT(__IO_PREFIX,ioportmap) (port);
}

extern inline void ioport_unmap(void __iomem *addr)
{
}

static inline void __iomem *ioremap(unsigned long port, unsigned long size)
{
	return IO_CONCAT(__IO_PREFIX,ioremap) (port, size);
}

static inline void __iomem *__ioremap(unsigned long port, unsigned long size,
				      unsigned long flags)
{
	return ioremap(port, size);
}

static inline void __iomem * ioremap_nocache(unsigned long offset,
					     unsigned long size)
{
	return ioremap(offset, size);
} 

static inline void iounmap(volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,iounmap)(addr);
}

static inline int __is_ioaddr(unsigned long addr)
{
	return IO_CONCAT(__IO_PREFIX,is_ioaddr)(addr);
}
#define __is_ioaddr(a)		__is_ioaddr((unsigned long)(a))

static inline int __is_mmio(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,is_mmio)(addr);
}


/*
 * If the actual I/O bits are sufficiently trivial, then expand inline.
 */

#if IO_CONCAT(__IO_PREFIX,trivial_io_bw)
extern inline unsigned int ioread8(void __iomem *addr)
{
	unsigned int ret = IO_CONCAT(__IO_PREFIX,ioread8)(addr);
	mb();
	return ret;
}

extern inline unsigned int ioread16(void __iomem *addr)
{
	unsigned int ret = IO_CONCAT(__IO_PREFIX,ioread16)(addr);
	mb();
	return ret;
}

extern inline void iowrite8(u8 b, void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,iowrite8)(b, addr);
	mb();
}

extern inline void iowrite16(u16 b, void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,iowrite16)(b, addr);
	mb();
}

extern inline u8 inb(unsigned long port)
{
	return ioread8(ioport_map(port, 1));
}

extern inline u16 inw(unsigned long port)
{
	return ioread16(ioport_map(port, 2));
}

extern inline void outb(u8 b, unsigned long port)
{
	iowrite8(b, ioport_map(port, 1));
}

extern inline void outw(u16 b, unsigned long port)
{
	iowrite16(b, ioport_map(port, 2));
}
#endif

#if IO_CONCAT(__IO_PREFIX,trivial_io_lq)
extern inline unsigned int ioread32(void __iomem *addr)
{
	unsigned int ret = IO_CONCAT(__IO_PREFIX,ioread32)(addr);
	mb();
	return ret;
}

extern inline void iowrite32(u32 b, void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,iowrite32)(b, addr);
	mb();
}

extern inline u32 inl(unsigned long port)
{
	return ioread32(ioport_map(port, 4));
}

extern inline void outl(u32 b, unsigned long port)
{
	iowrite32(b, ioport_map(port, 4));
}
#endif

#if IO_CONCAT(__IO_PREFIX,trivial_rw_bw) == 1
extern inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readb)(addr);
}

extern inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readw)(addr);
}

extern inline void __raw_writeb(u8 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writeb)(b, addr);
}

extern inline void __raw_writew(u16 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writew)(b, addr);
}

extern inline u8 readb(const volatile void __iomem *addr)
{
	u8 ret = __raw_readb(addr);
	mb();
	return ret;
}

extern inline u16 readw(const volatile void __iomem *addr)
{
	u16 ret = __raw_readw(addr);
	mb();
	return ret;
}

extern inline void writeb(u8 b, volatile void __iomem *addr)
{
	__raw_writeb(b, addr);
	mb();
}

extern inline void writew(u16 b, volatile void __iomem *addr)
{
	__raw_writew(b, addr);
	mb();
}
#endif

#if IO_CONCAT(__IO_PREFIX,trivial_rw_lq) == 1
extern inline u32 __raw_readl(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readl)(addr);
}

extern inline u64 __raw_readq(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readq)(addr);
}

extern inline void __raw_writel(u32 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writel)(b, addr);
}

extern inline void __raw_writeq(u64 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writeq)(b, addr);
}

extern inline u32 readl(const volatile void __iomem *addr)
{
	u32 ret = __raw_readl(addr);
	mb();
	return ret;
}

extern inline u64 readq(const volatile void __iomem *addr)
{
	u64 ret = __raw_readq(addr);
	mb();
	return ret;
}

extern inline void writel(u32 b, volatile void __iomem *addr)
{
	__raw_writel(b, addr);
	mb();
}

extern inline void writeq(u64 b, volatile void __iomem *addr)
{
	__raw_writeq(b, addr);
	mb();
}
#endif

#define inb_p		inb
#define inw_p		inw
#define inl_p		inl
#define outb_p		outb
#define outw_p		outw
#define outl_p		outl
#define readb_relaxed(addr) __raw_readb(addr)
#define readw_relaxed(addr) __raw_readw(addr)
#define readl_relaxed(addr) __raw_readl(addr)
#define readq_relaxed(addr) __raw_readq(addr)

#define mmiowb()

/*
 * String version of IO memory access ops:
 */
extern void memcpy_fromio(void *, const volatile void __iomem *, long);
extern void memcpy_toio(volatile void __iomem *, const void *, long);
extern void _memset_c_io(volatile void __iomem *, unsigned long, long);

static inline void memset_io(volatile void __iomem *addr, u8 c, long len)
{
	_memset_c_io(addr, 0x0101010101010101UL * c, len);
}

#define __HAVE_ARCH_MEMSETW_IO
static inline void memsetw_io(volatile void __iomem *addr, u16 c, long len)
{
	_memset_c_io(addr, 0x0001000100010001UL * c, len);
}

/*
 * String versions of in/out ops:
 */
extern void insb (unsigned long port, void *dst, unsigned long count);
extern void insw (unsigned long port, void *dst, unsigned long count);
extern void insl (unsigned long port, void *dst, unsigned long count);
extern void outsb (unsigned long port, const void *src, unsigned long count);
extern void outsw (unsigned long port, const void *src, unsigned long count);
extern void outsl (unsigned long port, const void *src, unsigned long count);

/*
 * The Alpha Jensen hardware for some rather strange reason puts
 * the RTC clock at 0x170 instead of 0x70. Probably due to some
 * misguided idea about using 0x70 for NMI stuff.
 *
 * These defines will override the defaults when doing RTC queries
 */

#ifdef CONFIG_ALPHA_GENERIC
# define RTC_PORT(x)	((x) + alpha_mv.rtc_port)
#else
# ifdef CONFIG_ALPHA_JENSEN
#  define RTC_PORT(x)	(0x170+(x))
# else
#  define RTC_PORT(x)	(0x70 + (x))
# endif
#endif
#define RTC_ALWAYS_BCD	0

/*
 * Some mucking forons use if[n]def writeq to check if platform has it.
 * It's a bloody bad idea and we probably want ARCH_HAS_WRITEQ for them
 * to play with; for now just use cpp anti-recursion logics and make sure
 * that damn thing is defined and expands to itself.
 */

#define writeq writeq
#define readq readq

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif /* __KERNEL__ */

#endif /* __ALPHA_IO_H */
