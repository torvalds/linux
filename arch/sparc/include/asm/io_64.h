#ifndef __SPARC64_IO_H
#define __SPARC64_IO_H

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/types.h>

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>
#include <asm/asi.h>

/* PC crapola... */
#define __SLOW_DOWN_IO	do { } while (0)
#define SLOW_DOWN_IO	do { } while (0)

/* BIO layer definitions. */
extern unsigned long kern_base, kern_size;
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

static inline u8 _inb(unsigned long addr)
{
	u8 ret;

	__asm__ __volatile__("lduba\t[%1] %2, %0\t/* pci_inb */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

static inline u16 _inw(unsigned long addr)
{
	u16 ret;

	__asm__ __volatile__("lduha\t[%1] %2, %0\t/* pci_inw */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

static inline u32 _inl(unsigned long addr)
{
	u32 ret;

	__asm__ __volatile__("lduwa\t[%1] %2, %0\t/* pci_inl */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

static inline void _outb(u8 b, unsigned long addr)
{
	__asm__ __volatile__("stba\t%r0, [%1] %2\t/* pci_outb */"
			     : /* no outputs */
			     : "Jr" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

static inline void _outw(u16 w, unsigned long addr)
{
	__asm__ __volatile__("stha\t%r0, [%1] %2\t/* pci_outw */"
			     : /* no outputs */
			     : "Jr" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

static inline void _outl(u32 l, unsigned long addr)
{
	__asm__ __volatile__("stwa\t%r0, [%1] %2\t/* pci_outl */"
			     : /* no outputs */
			     : "Jr" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

#define inb(__addr)		(_inb((unsigned long)(__addr)))
#define inw(__addr)		(_inw((unsigned long)(__addr)))
#define inl(__addr)		(_inl((unsigned long)(__addr)))
#define outb(__b, __addr)	(_outb((u8)(__b), (unsigned long)(__addr)))
#define outw(__w, __addr)	(_outw((u16)(__w), (unsigned long)(__addr)))
#define outl(__l, __addr)	(_outl((u32)(__l), (unsigned long)(__addr)))

#define inb_p(__addr) 		inb(__addr)
#define outb_p(__b, __addr)	outb(__b, __addr)
#define inw_p(__addr)		inw(__addr)
#define outw_p(__w, __addr)	outw(__w, __addr)
#define inl_p(__addr)		inl(__addr)
#define outl_p(__l, __addr)	outl(__l, __addr)

extern void outsb(unsigned long, const void *, unsigned long);
extern void outsw(unsigned long, const void *, unsigned long);
extern void outsl(unsigned long, const void *, unsigned long);
extern void insb(unsigned long, void *, unsigned long);
extern void insw(unsigned long, void *, unsigned long);
extern void insl(unsigned long, void *, unsigned long);

static inline void ioread8_rep(void __iomem *port, void *buf, unsigned long count)
{
	insb((unsigned long __force)port, buf, count);
}
static inline void ioread16_rep(void __iomem *port, void *buf, unsigned long count)
{
	insw((unsigned long __force)port, buf, count);
}

static inline void ioread32_rep(void __iomem *port, void *buf, unsigned long count)
{
	insl((unsigned long __force)port, buf, count);
}

static inline void iowrite8_rep(void __iomem *port, const void *buf, unsigned long count)
{
	outsb((unsigned long __force)port, buf, count);
}

static inline void iowrite16_rep(void __iomem *port, const void *buf, unsigned long count)
{
	outsw((unsigned long __force)port, buf, count);
}

static inline void iowrite32_rep(void __iomem *port, const void *buf, unsigned long count)
{
	outsl((unsigned long __force)port, buf, count);
}

/* Memory functions, same as I/O accesses on Ultra. */
static inline u8 _readb(const volatile void __iomem *addr)
{	u8 ret;

	__asm__ __volatile__("lduba\t[%1] %2, %0\t/* pci_readb */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	return ret;
}

static inline u16 _readw(const volatile void __iomem *addr)
{	u16 ret;

	__asm__ __volatile__("lduha\t[%1] %2, %0\t/* pci_readw */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

static inline u32 _readl(const volatile void __iomem *addr)
{	u32 ret;

	__asm__ __volatile__("lduwa\t[%1] %2, %0\t/* pci_readl */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

static inline u64 _readq(const volatile void __iomem *addr)
{	u64 ret;

	__asm__ __volatile__("ldxa\t[%1] %2, %0\t/* pci_readq */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

static inline void _writeb(u8 b, volatile void __iomem *addr)
{
	__asm__ __volatile__("stba\t%r0, [%1] %2\t/* pci_writeb */"
			     : /* no outputs */
			     : "Jr" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

static inline void _writew(u16 w, volatile void __iomem *addr)
{
	__asm__ __volatile__("stha\t%r0, [%1] %2\t/* pci_writew */"
			     : /* no outputs */
			     : "Jr" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

static inline void _writel(u32 l, volatile void __iomem *addr)
{
	__asm__ __volatile__("stwa\t%r0, [%1] %2\t/* pci_writel */"
			     : /* no outputs */
			     : "Jr" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

static inline void _writeq(u64 q, volatile void __iomem *addr)
{
	__asm__ __volatile__("stxa\t%r0, [%1] %2\t/* pci_writeq */"
			     : /* no outputs */
			     : "Jr" (q), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

#define readb(__addr)		_readb(__addr)
#define readw(__addr)		_readw(__addr)
#define readl(__addr)		_readl(__addr)
#define readq(__addr)		_readq(__addr)
#define readb_relaxed(__addr)	_readb(__addr)
#define readw_relaxed(__addr)	_readw(__addr)
#define readl_relaxed(__addr)	_readl(__addr)
#define readq_relaxed(__addr)	_readq(__addr)
#define writeb(__b, __addr)	_writeb(__b, __addr)
#define writew(__w, __addr)	_writew(__w, __addr)
#define writel(__l, __addr)	_writel(__l, __addr)
#define writeq(__q, __addr)	_writeq(__q, __addr)

/* Now versions without byte-swapping. */
static inline u8 _raw_readb(unsigned long addr)
{
	u8 ret;

	__asm__ __volatile__("lduba\t[%1] %2, %0\t/* pci_raw_readb */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

static inline u16 _raw_readw(unsigned long addr)
{
	u16 ret;

	__asm__ __volatile__("lduha\t[%1] %2, %0\t/* pci_raw_readw */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

static inline u32 _raw_readl(unsigned long addr)
{
	u32 ret;

	__asm__ __volatile__("lduwa\t[%1] %2, %0\t/* pci_raw_readl */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

static inline u64 _raw_readq(unsigned long addr)
{
	u64 ret;

	__asm__ __volatile__("ldxa\t[%1] %2, %0\t/* pci_raw_readq */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

static inline void _raw_writeb(u8 b, unsigned long addr)
{
	__asm__ __volatile__("stba\t%r0, [%1] %2\t/* pci_raw_writeb */"
			     : /* no outputs */
			     : "Jr" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

static inline void _raw_writew(u16 w, unsigned long addr)
{
	__asm__ __volatile__("stha\t%r0, [%1] %2\t/* pci_raw_writew */"
			     : /* no outputs */
			     : "Jr" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

static inline void _raw_writel(u32 l, unsigned long addr)
{
	__asm__ __volatile__("stwa\t%r0, [%1] %2\t/* pci_raw_writel */"
			     : /* no outputs */
			     : "Jr" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

static inline void _raw_writeq(u64 q, unsigned long addr)
{
	__asm__ __volatile__("stxa\t%r0, [%1] %2\t/* pci_raw_writeq */"
			     : /* no outputs */
			     : "Jr" (q), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

#define __raw_readb(__addr)		(_raw_readb((unsigned long)(__addr)))
#define __raw_readw(__addr)		(_raw_readw((unsigned long)(__addr)))
#define __raw_readl(__addr)		(_raw_readl((unsigned long)(__addr)))
#define __raw_readq(__addr)		(_raw_readq((unsigned long)(__addr)))
#define __raw_writeb(__b, __addr)	(_raw_writeb((u8)(__b), (unsigned long)(__addr)))
#define __raw_writew(__w, __addr)	(_raw_writew((u16)(__w), (unsigned long)(__addr)))
#define __raw_writel(__l, __addr)	(_raw_writel((u32)(__l), (unsigned long)(__addr)))
#define __raw_writeq(__q, __addr)	(_raw_writeq((u64)(__q), (unsigned long)(__addr)))

/* Valid I/O Space regions are anywhere, because each PCI bus supported
 * can live in an arbitrary area of the physical address range.
 */
#define IO_SPACE_LIMIT 0xffffffffffffffffUL

/* Now, SBUS variants, only difference from PCI is that we do
 * not use little-endian ASIs.
 */
static inline u8 _sbus_readb(const volatile void __iomem *addr)
{
	u8 ret;

	__asm__ __volatile__("lduba\t[%1] %2, %0\t/* sbus_readb */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");

	return ret;
}

static inline u16 _sbus_readw(const volatile void __iomem *addr)
{
	u16 ret;

	__asm__ __volatile__("lduha\t[%1] %2, %0\t/* sbus_readw */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");

	return ret;
}

static inline u32 _sbus_readl(const volatile void __iomem *addr)
{
	u32 ret;

	__asm__ __volatile__("lduwa\t[%1] %2, %0\t/* sbus_readl */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");

	return ret;
}

static inline u64 _sbus_readq(const volatile void __iomem *addr)
{
	u64 ret;

	__asm__ __volatile__("ldxa\t[%1] %2, %0\t/* sbus_readq */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");

	return ret;
}

static inline void _sbus_writeb(u8 b, volatile void __iomem *addr)
{
	__asm__ __volatile__("stba\t%r0, [%1] %2\t/* sbus_writeb */"
			     : /* no outputs */
			     : "Jr" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");
}

static inline void _sbus_writew(u16 w, volatile void __iomem *addr)
{
	__asm__ __volatile__("stha\t%r0, [%1] %2\t/* sbus_writew */"
			     : /* no outputs */
			     : "Jr" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");
}

static inline void _sbus_writel(u32 l, volatile void __iomem *addr)
{
	__asm__ __volatile__("stwa\t%r0, [%1] %2\t/* sbus_writel */"
			     : /* no outputs */
			     : "Jr" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");
}

static inline void _sbus_writeq(u64 l, volatile void __iomem *addr)
{
	__asm__ __volatile__("stxa\t%r0, [%1] %2\t/* sbus_writeq */"
			     : /* no outputs */
			     : "Jr" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E)
			     : "memory");
}

#define sbus_readb(__addr)		_sbus_readb(__addr)
#define sbus_readw(__addr)		_sbus_readw(__addr)
#define sbus_readl(__addr)		_sbus_readl(__addr)
#define sbus_readq(__addr)		_sbus_readq(__addr)
#define sbus_writeb(__b, __addr)	_sbus_writeb(__b, __addr)
#define sbus_writew(__w, __addr)	_sbus_writew(__w, __addr)
#define sbus_writel(__l, __addr)	_sbus_writel(__l, __addr)
#define sbus_writeq(__l, __addr)	_sbus_writeq(__l, __addr)

static inline void _sbus_memset_io(volatile void __iomem *dst, int c, __kernel_size_t n)
{
	while(n--) {
		sbus_writeb(c, dst);
		dst++;
	}
}

#define sbus_memset_io(d,c,sz)	_sbus_memset_io(d,c,sz)

static inline void
_memset_io(volatile void __iomem *dst, int c, __kernel_size_t n)
{
	volatile void __iomem *d = dst;

	while (n--) {
		writeb(c, d);
		d++;
	}
}

#define memset_io(d,c,sz)	_memset_io(d,c,sz)

static inline void
_sbus_memcpy_fromio(void *dst, const volatile void __iomem *src,
		    __kernel_size_t n)
{
	char *d = dst;

	while (n--) {
		char tmp = sbus_readb(src);
		*d++ = tmp;
		src++;
	}
}

#define sbus_memcpy_fromio(d, s, sz)	_sbus_memcpy_fromio(d, s, sz)

static inline void
_memcpy_fromio(void *dst, const volatile void __iomem *src, __kernel_size_t n)
{
	char *d = dst;

	while (n--) {
		char tmp = readb(src);
		*d++ = tmp;
		src++;
	}
}

#define memcpy_fromio(d,s,sz)	_memcpy_fromio(d,s,sz)

static inline void
_sbus_memcpy_toio(volatile void __iomem *dst, const void *src,
		  __kernel_size_t n)
{
	const char *s = src;
	volatile void __iomem *d = dst;

	while (n--) {
		char tmp = *s++;
		sbus_writeb(tmp, d);
		d++;
	}
}

#define sbus_memcpy_toio(d, s, sz)	_sbus_memcpy_toio(d, s, sz)

static inline void
_memcpy_toio(volatile void __iomem *dst, const void *src, __kernel_size_t n)
{
	const char *s = src;
	volatile void __iomem *d = dst;

	while (n--) {
		char tmp = *s++;
		writeb(tmp, d);
		d++;
	}
}

#define memcpy_toio(d,s,sz)	_memcpy_toio(d,s,sz)

#define mmiowb()

#ifdef __KERNEL__

/* On sparc64 we have the whole physical IO address space accessible
 * using physically addressed loads and stores, so this does nothing.
 */
static inline void __iomem *ioremap(unsigned long offset, unsigned long size)
{
	return (void __iomem *)offset;
}

#define ioremap_nocache(X,Y)		ioremap((X),(Y))
#define ioremap_wc(X,Y)			ioremap((X),(Y))

static inline void iounmap(volatile void __iomem *addr)
{
}

#define ioread8(X)			readb(X)
#define ioread16(X)			readw(X)
#define ioread16be(X)			__raw_readw(X)
#define ioread32(X)			readl(X)
#define ioread32be(X)			__raw_readl(X)
#define iowrite8(val,X)			writeb(val,X)
#define iowrite16(val,X)		writew(val,X)
#define iowrite16be(val,X)		__raw_writew(val,X)
#define iowrite32(val,X)		writel(val,X)
#define iowrite32be(val,X)		__raw_writel(val,X)

/* Create a virtual mapping cookie for an IO port range */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *);

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
extern void pci_iounmap(struct pci_dev *dev, void __iomem *);

static inline int sbus_can_dma_64bit(void)
{
	return 1;
}
static inline int sbus_can_burst64(void)
{
	return 1;
}
struct device;
extern void sbus_set_sbus64(struct device *, int);

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif

#endif /* !(__SPARC64_IO_H) */
