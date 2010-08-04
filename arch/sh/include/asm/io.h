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
 *
 * Historically, we have also had ctrl_in{b,w,l,q}/ctrl_out{b,w,l,q} for
 * SuperH specific I/O (raw I/O to on-chip CPU peripherals). In practice
 * these have the same semantics as the __raw variants, and as such, all
 * new code should be using the __raw versions.
 *
 * All ISA I/O routines are wrapped through the machine vector. If a
 * board does not provide overrides, a generic set that are copied in
 * from the default machine vector are used instead. These are largely
 * for old compat code for I/O offseting to SuperIOs, all of which are
 * better handled through the machvec ioport mapping routines these days.
 */
#include <linux/errno.h>
#include <asm/cache.h>
#include <asm/system.h>
#include <asm/addrspace.h>
#include <asm/machvec.h>
#include <asm/pgtable.h>
#include <asm-generic/iomap.h>

#ifdef __KERNEL__
/*
 * Depending on which platform we are running on, we need different
 * I/O functions.
 */
#define __IO_PREFIX	generic
#include <asm/io_generic.h>
#include <asm/io_trapped.h>

#ifdef CONFIG_HAS_IOPORT

#define inb(p)			sh_mv.mv_inb((p))
#define inw(p)			sh_mv.mv_inw((p))
#define inl(p)			sh_mv.mv_inl((p))
#define outb(x,p)		sh_mv.mv_outb((x),(p))
#define outw(x,p)		sh_mv.mv_outw((x),(p))
#define outl(x,p)		sh_mv.mv_outl((x),(p))

#define inb_p(p)		sh_mv.mv_inb_p((p))
#define inw_p(p)		sh_mv.mv_inw_p((p))
#define inl_p(p)		sh_mv.mv_inl_p((p))
#define outb_p(x,p)		sh_mv.mv_outb_p((x),(p))
#define outw_p(x,p)		sh_mv.mv_outw_p((x),(p))
#define outl_p(x,p)		sh_mv.mv_outl_p((x),(p))

#define insb(p,b,c)		sh_mv.mv_insb((p), (b), (c))
#define insw(p,b,c)		sh_mv.mv_insw((p), (b), (c))
#define insl(p,b,c)		sh_mv.mv_insl((p), (b), (c))
#define outsb(p,b,c)		sh_mv.mv_outsb((p), (b), (c))
#define outsw(p,b,c)		sh_mv.mv_outsw((p), (b), (c))
#define outsl(p,b,c)		sh_mv.mv_outsl((p), (b), (c))

#endif

#define __raw_writeb(v,a)	(__chk_io_ptr(a), *(volatile u8  __force *)(a) = (v))
#define __raw_writew(v,a)	(__chk_io_ptr(a), *(volatile u16 __force *)(a) = (v))
#define __raw_writel(v,a)	(__chk_io_ptr(a), *(volatile u32 __force *)(a) = (v))
#define __raw_writeq(v,a)	(__chk_io_ptr(a), *(volatile u64 __force *)(a) = (v))

#define __raw_readb(a)		(__chk_io_ptr(a), *(volatile u8  __force *)(a))
#define __raw_readw(a)		(__chk_io_ptr(a), *(volatile u16 __force *)(a))
#define __raw_readl(a)		(__chk_io_ptr(a), *(volatile u32 __force *)(a))
#define __raw_readq(a)		(__chk_io_ptr(a), *(volatile u64 __force *)(a))

#define readb(a)		({ u8  r_ = __raw_readb(a); mb(); r_; })
#define readw(a)		({ u16 r_ = __raw_readw(a); mb(); r_; })
#define readl(a)		({ u32 r_ = __raw_readl(a); mb(); r_; })
#define readq(a)		({ u64 r_ = __raw_readq(a); mb(); r_; })

#define writeb(v,a)		({ __raw_writeb((v),(a)); mb(); })
#define writew(v,a)		({ __raw_writew((v),(a)); mb(); })
#define writel(v,a)		({ __raw_writel((v),(a)); mb(); })
#define writeq(v,a)		({ __raw_writeq((v),(a)); mb(); })

/*
 * Legacy SuperH on-chip I/O functions
 *
 * These are all deprecated, all new (and especially cross-platform) code
 * should be using the __raw_xxx() routines directly.
 */
static inline u8 __deprecated ctrl_inb(unsigned long addr)
{
	return __raw_readb(addr);
}

static inline u16 __deprecated ctrl_inw(unsigned long addr)
{
	return __raw_readw(addr);
}

static inline u32 __deprecated ctrl_inl(unsigned long addr)
{
	return __raw_readl(addr);
}

static inline u64 __deprecated ctrl_inq(unsigned long addr)
{
	return __raw_readq(addr);
}

static inline void __deprecated ctrl_outb(u8 v, unsigned long addr)
{
	__raw_writeb(v, addr);
}

static inline void __deprecated ctrl_outw(u16 v, unsigned long addr)
{
	__raw_writew(v, addr);
}

static inline void __deprecated ctrl_outl(u32 v, unsigned long addr)
{
	__raw_writel(v, addr);
}

static inline void __deprecated ctrl_outq(u64 v, unsigned long addr)
{
	__raw_writeq(v, addr);
}

extern unsigned long generic_io_base;

static inline void ctrl_delay(void)
{
	__raw_readw(generic_io_base);
}

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

#define __BUILD_MEMORY_STRING(bwlq, type)				\
									\
static inline void __raw_writes##bwlq(volatile void __iomem *mem,	\
				const void *addr, unsigned int count)	\
{									\
	const volatile type *__addr = addr;				\
									\
	while (count--) {						\
		__raw_write##bwlq(*__addr, mem);			\
		__addr++;						\
	}								\
}									\
									\
static inline void __raw_reads##bwlq(volatile void __iomem *mem,	\
			       void *addr, unsigned int count)		\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		*__addr = __raw_read##bwlq(mem);			\
		__addr++;						\
	}								\
}

__BUILD_MEMORY_STRING(b, u8)
__BUILD_MEMORY_STRING(w, u16)

#ifdef CONFIG_SUPERH32
void __raw_writesl(void __iomem *addr, const void *data, int longlen);
void __raw_readsl(const void __iomem *addr, void *data, int longlen);
#else
__BUILD_MEMORY_STRING(l, u32)
#endif

__BUILD_MEMORY_STRING(q, u64)

#define writesb			__raw_writesb
#define writesw			__raw_writesw
#define writesl			__raw_writesl

#define readsb			__raw_readsb
#define readsw			__raw_readsw
#define readsl			__raw_readsl

#define readb_relaxed(a)	readb(a)
#define readw_relaxed(a)	readw(a)
#define readl_relaxed(a)	readl(a)
#define readq_relaxed(a)	readq(a)

#ifndef CONFIG_GENERIC_IOMAP
/* Simple MMIO */
#define ioread8(a)		__raw_readb(a)
#define ioread16(a)		__raw_readw(a)
#define ioread16be(a)		be16_to_cpu(__raw_readw((a)))
#define ioread32(a)		__raw_readl(a)
#define ioread32be(a)		be32_to_cpu(__raw_readl((a)))

#define iowrite8(v,a)		__raw_writeb((v),(a))
#define iowrite16(v,a)		__raw_writew((v),(a))
#define iowrite16be(v,a)	__raw_writew(cpu_to_be16((v)),(a))
#define iowrite32(v,a)		__raw_writel((v),(a))
#define iowrite32be(v,a)	__raw_writel(cpu_to_be32((v)),(a))

#define ioread8_rep(a, d, c)	__raw_readsb((a), (d), (c))
#define ioread16_rep(a, d, c)	__raw_readsw((a), (d), (c))
#define ioread32_rep(a, d, c)	__raw_readsl((a), (d), (c))

#define iowrite8_rep(a, s, c)	__raw_writesb((a), (s), (c))
#define iowrite16_rep(a, s, c)	__raw_writesw((a), (s), (c))
#define iowrite32_rep(a, s, c)	__raw_writesl((a), (s), (c))
#endif

#define mmio_insb(p,d,c)	__raw_readsb(p,d,c)
#define mmio_insw(p,d,c)	__raw_readsw(p,d,c)
#define mmio_insl(p,d,c)	__raw_readsl(p,d,c)

#define mmio_outsb(p,s,c)	__raw_writesb(p,s,c)
#define mmio_outsw(p,s,c)	__raw_writesw(p,s,c)
#define mmio_outsl(p,s,c)	__raw_writesl(p,s,c)

/* synco on SH-4A, otherwise a nop */
#define mmiowb()		wmb()

#define IO_SPACE_LIMIT 0xffffffff

#ifdef CONFIG_HAS_IOPORT

/*
 * This function provides a method for the generic case where a
 * board-specific ioport_map simply needs to return the port + some
 * arbitrary port base.
 *
 * We use this at board setup time to implicitly set the port base, and
 * as a result, we can use the generic ioport_map.
 */
static inline void __set_io_port_base(unsigned long pbase)
{
	generic_io_base = pbase;
}

#define __ioport_map(p, n) sh_mv.mv_ioport_map((p), (n))

#endif

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
		if (unlikely(pgprot_val(prot) & _PAGE_CACHABLE))
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

#define maybebadio(port) \
	printk(KERN_ERR "bad PC-like io %s:%u for port 0x%lx at 0x%08x\n", \
	       __func__, __LINE__, (port), (u32)__builtin_return_address(0))

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
