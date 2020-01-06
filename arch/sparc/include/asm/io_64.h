/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC64_IO_H
#define __SPARC64_IO_H

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/types.h>

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/asi.h>
#include <asm-generic/pci_iomap.h>

/* BIO layer definitions. */
extern unsigned long kern_base, kern_size;

/* __raw_{read,write}{b,w,l,q} uses direct access.
 * Access the memory as big endian bypassing the cache
 * by using ASI_PHYS_BYPASS_EC_E
 */
#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 ret;

	__asm__ __volatile__("lduba\t[%1] %2, %0\t/* pci_raw_readb */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 ret;

	__asm__ __volatile__("lduha\t[%1] %2, %0\t/* pci_raw_readw */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 ret;

	__asm__ __volatile__("lduwa\t[%1] %2, %0\t/* pci_raw_readl */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

#define __raw_readq __raw_readq
static inline u64 __raw_readq(const volatile void __iomem *addr)
{
	u64 ret;

	__asm__ __volatile__("ldxa\t[%1] %2, %0\t/* pci_raw_readq */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 b, const volatile void __iomem *addr)
{
	__asm__ __volatile__("stba\t%r0, [%1] %2\t/* pci_raw_writeb */"
			     : /* no outputs */
			     : "Jr" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 w, const volatile void __iomem *addr)
{
	__asm__ __volatile__("stha\t%r0, [%1] %2\t/* pci_raw_writew */"
			     : /* no outputs */
			     : "Jr" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 l, const volatile void __iomem *addr)
{
	__asm__ __volatile__("stwa\t%r0, [%1] %2\t/* pci_raw_writel */"
			     : /* no outputs */
			     : "Jr" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

#define __raw_writeq __raw_writeq
static inline void __raw_writeq(u64 q, const volatile void __iomem *addr)
{
	__asm__ __volatile__("stxa\t%r0, [%1] %2\t/* pci_raw_writeq */"
			     : /* no outputs */
			     : "Jr" (q), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

/* Memory functions, same as I/O accesses on Ultra.
 * Access memory as little endian bypassing
 * the cache by using ASI_PHYS_BYPASS_EC_E_L
 */
#define readb readb
#define readb_relaxed readb
static inline u8 readb(const volatile void __iomem *addr)
{	u8 ret;

	__asm__ __volatile__("lduba\t[%1] %2, %0\t/* pci_readb */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	return ret;
}

#define readw readw
#define readw_relaxed readw
static inline u16 readw(const volatile void __iomem *addr)
{	u16 ret;

	__asm__ __volatile__("lduha\t[%1] %2, %0\t/* pci_readw */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

#define readl readl
#define readl_relaxed readl
static inline u32 readl(const volatile void __iomem *addr)
{	u32 ret;

	__asm__ __volatile__("lduwa\t[%1] %2, %0\t/* pci_readl */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

#define readq readq
#define readq_relaxed readq
static inline u64 readq(const volatile void __iomem *addr)
{	u64 ret;

	__asm__ __volatile__("ldxa\t[%1] %2, %0\t/* pci_readq */"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	return ret;
}

#define writeb writeb
#define writeb_relaxed writeb
static inline void writeb(u8 b, volatile void __iomem *addr)
{
	__asm__ __volatile__("stba\t%r0, [%1] %2\t/* pci_writeb */"
			     : /* no outputs */
			     : "Jr" (b), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

#define writew writew
#define writew_relaxed writew
static inline void writew(u16 w, volatile void __iomem *addr)
{
	__asm__ __volatile__("stha\t%r0, [%1] %2\t/* pci_writew */"
			     : /* no outputs */
			     : "Jr" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

#define writel writel
#define writel_relaxed writel
static inline void writel(u32 l, volatile void __iomem *addr)
{
	__asm__ __volatile__("stwa\t%r0, [%1] %2\t/* pci_writel */"
			     : /* no outputs */
			     : "Jr" (l), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

#define writeq writeq
#define writeq_relaxed writeq
static inline void writeq(u64 q, volatile void __iomem *addr)
{
	__asm__ __volatile__("stxa\t%r0, [%1] %2\t/* pci_writeq */"
			     : /* no outputs */
			     : "Jr" (q), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
}

#define inb inb
static inline u8 inb(unsigned long addr)
{
	return readb((volatile void __iomem *)addr);
}

#define inw inw
static inline u16 inw(unsigned long addr)
{
	return readw((volatile void __iomem *)addr);
}

#define inl inl
static inline u32 inl(unsigned long addr)
{
	return readl((volatile void __iomem *)addr);
}

#define outb outb
static inline void outb(u8 b, unsigned long addr)
{
	writeb(b, (volatile void __iomem *)addr);
}

#define outw outw
static inline void outw(u16 w, unsigned long addr)
{
	writew(w, (volatile void __iomem *)addr);
}

#define outl outl
static inline void outl(u32 l, unsigned long addr)
{
	writel(l, (volatile void __iomem *)addr);
}


#define inb_p(__addr) 		inb(__addr)
#define outb_p(__b, __addr)	outb(__b, __addr)
#define inw_p(__addr)		inw(__addr)
#define outw_p(__w, __addr)	outw(__w, __addr)
#define inl_p(__addr)		inl(__addr)
#define outl_p(__l, __addr)	outl(__l, __addr)

void outsb(unsigned long, const void *, unsigned long);
void outsw(unsigned long, const void *, unsigned long);
void outsl(unsigned long, const void *, unsigned long);
void insb(unsigned long, void *, unsigned long);
void insw(unsigned long, void *, unsigned long);
void insl(unsigned long, void *, unsigned long);

static inline void readsb(void __iomem *port, void *buf, unsigned long count)
{
	insb((unsigned long __force)port, buf, count);
}
static inline void readsw(void __iomem *port, void *buf, unsigned long count)
{
	insw((unsigned long __force)port, buf, count);
}

static inline void readsl(void __iomem *port, void *buf, unsigned long count)
{
	insl((unsigned long __force)port, buf, count);
}

static inline void writesb(void __iomem *port, const void *buf, unsigned long count)
{
	outsb((unsigned long __force)port, buf, count);
}

static inline void writesw(void __iomem *port, const void *buf, unsigned long count)
{
	outsw((unsigned long __force)port, buf, count);
}

static inline void writesl(void __iomem *port, const void *buf, unsigned long count)
{
	outsl((unsigned long __force)port, buf, count);
}

#define ioread8_rep(p,d,l)	readsb(p,d,l)
#define ioread16_rep(p,d,l)	readsw(p,d,l)
#define ioread32_rep(p,d,l)	readsl(p,d,l)
#define iowrite8_rep(p,d,l)	writesb(p,d,l)
#define iowrite16_rep(p,d,l)	writesw(p,d,l)
#define iowrite32_rep(p,d,l)	writesl(p,d,l)

/* Valid I/O Space regions are anywhere, because each PCI bus supported
 * can live in an arbitrary area of the physical address range.
 */
#define IO_SPACE_LIMIT 0xffffffffffffffffUL

/* Now, SBUS variants, only difference from PCI is that we do
 * not use little-endian ASIs.
 */
static inline u8 sbus_readb(const volatile void __iomem *addr)
{
	return __raw_readb(addr);
}

static inline u16 sbus_readw(const volatile void __iomem *addr)
{
	return __raw_readw(addr);
}

static inline u32 sbus_readl(const volatile void __iomem *addr)
{
	return __raw_readl(addr);
}

static inline u64 sbus_readq(const volatile void __iomem *addr)
{
	return __raw_readq(addr);
}

static inline void sbus_writeb(u8 b, volatile void __iomem *addr)
{
	__raw_writeb(b, addr);
}

static inline void sbus_writew(u16 w, volatile void __iomem *addr)
{
	__raw_writew(w, addr);
}

static inline void sbus_writel(u32 l, volatile void __iomem *addr)
{
	__raw_writel(l, addr);
}

static inline void sbus_writeq(u64 q, volatile void __iomem *addr)
{
	__raw_writeq(q, addr);
}

static inline void sbus_memset_io(volatile void __iomem *dst, int c, __kernel_size_t n)
{
	while(n--) {
		sbus_writeb(c, dst);
		dst++;
	}
}

static inline void memset_io(volatile void __iomem *dst, int c, __kernel_size_t n)
{
	volatile void __iomem *d = dst;

	while (n--) {
		writeb(c, d);
		d++;
	}
}

static inline void sbus_memcpy_fromio(void *dst, const volatile void __iomem *src,
				      __kernel_size_t n)
{
	char *d = dst;

	while (n--) {
		char tmp = sbus_readb(src);
		*d++ = tmp;
		src++;
	}
}


static inline void memcpy_fromio(void *dst, const volatile void __iomem *src,
				 __kernel_size_t n)
{
	char *d = dst;

	while (n--) {
		char tmp = readb(src);
		*d++ = tmp;
		src++;
	}
}

static inline void sbus_memcpy_toio(volatile void __iomem *dst, const void *src,
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

static inline void memcpy_toio(volatile void __iomem *dst, const void *src,
			       __kernel_size_t n)
{
	const char *s = src;
	volatile void __iomem *d = dst;

	while (n--) {
		char tmp = *s++;
		writeb(tmp, d);
		d++;
	}
}

#ifdef __KERNEL__

/* On sparc64 we have the whole physical IO address space accessible
 * using physically addressed loads and stores, so this does nothing.
 */
static inline void __iomem *ioremap(unsigned long offset, unsigned long size)
{
	return (void __iomem *)offset;
}

#define ioremap_uc(X,Y)			ioremap((X),(Y))
#define ioremap_wc(X,Y)			ioremap((X),(Y))
#define ioremap_wt(X,Y)			ioremap((X),(Y))

static inline void iounmap(volatile void __iomem *addr)
{
}

#define ioread8			readb
#define ioread16		readw
#define ioread16be		__raw_readw
#define ioread32		readl
#define ioread32be		__raw_readl
#define iowrite8		writeb
#define iowrite16		writew
#define iowrite16be		__raw_writew
#define iowrite32		writel
#define iowrite32be		__raw_writel

/* Create a virtual mapping cookie for an IO port range */
void __iomem *ioport_map(unsigned long port, unsigned int nr);
void ioport_unmap(void __iomem *);

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
void pci_iounmap(struct pci_dev *dev, void __iomem *);

static inline int sbus_can_dma_64bit(void)
{
	return 1;
}
static inline int sbus_can_burst64(void)
{
	return 1;
}
struct device;
void sbus_set_sbus64(struct device *, int);

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
