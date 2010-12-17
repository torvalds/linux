#ifndef __SPARC_IO_H
#define __SPARC_IO_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>  /* struct resource */

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>

#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

static inline u32 flip_dword (u32 l)
{
	return ((l&0xff)<<24) | (((l>>8)&0xff)<<16) | (((l>>16)&0xff)<<8)| ((l>>24)&0xff);
}

static inline u16 flip_word (u16 w)
{
	return ((w&0xff) << 8) | ((w>>8)&0xff);
}

#define mmiowb()

/*
 * Memory mapped I/O to PCI
 */

static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return *(__force volatile u8 *)addr;
}

static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return *(__force volatile u16 *)addr;
}

static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	return *(__force volatile u32 *)addr;
}

static inline void __raw_writeb(u8 b, volatile void __iomem *addr)
{
	*(__force volatile u8 *)addr = b;
}

static inline void __raw_writew(u16 w, volatile void __iomem *addr)
{
	*(__force volatile u16 *)addr = w;
}

static inline void __raw_writel(u32 l, volatile void __iomem *addr)
{
	*(__force volatile u32 *)addr = l;
}

static inline u8 __readb(const volatile void __iomem *addr)
{
	return *(__force volatile u8 *)addr;
}

static inline u16 __readw(const volatile void __iomem *addr)
{
	return flip_word(*(__force volatile u16 *)addr);
}

static inline u32 __readl(const volatile void __iomem *addr)
{
	return flip_dword(*(__force volatile u32 *)addr);
}

static inline void __writeb(u8 b, volatile void __iomem *addr)
{
	*(__force volatile u8 *)addr = b;
}

static inline void __writew(u16 w, volatile void __iomem *addr)
{
	*(__force volatile u16 *)addr = flip_word(w);
}

static inline void __writel(u32 l, volatile void __iomem *addr)
{
	*(__force volatile u32 *)addr = flip_dword(l);
}

#define readb(__addr)		__readb(__addr)
#define readw(__addr)		__readw(__addr)
#define readl(__addr)		__readl(__addr)
#define readb_relaxed(__addr)	readb(__addr)
#define readw_relaxed(__addr)	readw(__addr)
#define readl_relaxed(__addr)	readl(__addr)

#define writeb(__b, __addr)	__writeb((__b),(__addr))
#define writew(__w, __addr)	__writew((__w),(__addr))
#define writel(__l, __addr)	__writel((__l),(__addr))

/*
 * I/O space operations
 *
 * Arrangement on a Sun is somewhat complicated.
 *
 * First of all, we want to use standard Linux drivers
 * for keyboard, PC serial, etc. These drivers think
 * they access I/O space and use inb/outb.
 * On the other hand, EBus bridge accepts PCI *memory*
 * cycles and converts them into ISA *I/O* cycles.
 * Ergo, we want inb & outb to generate PCI memory cycles.
 *
 * If we want to issue PCI *I/O* cycles, we do this
 * with a low 64K fixed window in PCIC. This window gets
 * mapped somewhere into virtual kernel space and we
 * can use inb/outb again.
 */
#define inb_local(__addr)	__readb((void __iomem *)(unsigned long)(__addr))
#define inb(__addr)		__readb((void __iomem *)(unsigned long)(__addr))
#define inw(__addr)		__readw((void __iomem *)(unsigned long)(__addr))
#define inl(__addr)		__readl((void __iomem *)(unsigned long)(__addr))

#define outb_local(__b, __addr)	__writeb(__b, (void __iomem *)(unsigned long)(__addr))
#define outb(__b, __addr)	__writeb(__b, (void __iomem *)(unsigned long)(__addr))
#define outw(__w, __addr)	__writew(__w, (void __iomem *)(unsigned long)(__addr))
#define outl(__l, __addr)	__writel(__l, (void __iomem *)(unsigned long)(__addr))

#define inb_p(__addr)		inb(__addr)
#define outb_p(__b, __addr)	outb(__b, __addr)
#define inw_p(__addr)		inw(__addr)
#define outw_p(__w, __addr)	outw(__w, __addr)
#define inl_p(__addr)		inl(__addr)
#define outl_p(__l, __addr)	outl(__l, __addr)

void outsb(unsigned long addr, const void *src, unsigned long cnt);
void outsw(unsigned long addr, const void *src, unsigned long cnt);
void outsl(unsigned long addr, const void *src, unsigned long cnt);
void insb(unsigned long addr, void *dst, unsigned long count);
void insw(unsigned long addr, void *dst, unsigned long count);
void insl(unsigned long addr, void *dst, unsigned long count);

#define IO_SPACE_LIMIT 0xffffffff

/*
 * SBus accessors.
 *
 * SBus has only one, memory mapped, I/O space.
 * We do not need to flip bytes for SBus of course.
 */
static inline u8 _sbus_readb(const volatile void __iomem *addr)
{
	return *(__force volatile u8 *)addr;
}

static inline u16 _sbus_readw(const volatile void __iomem *addr)
{
	return *(__force volatile u16 *)addr;
}

static inline u32 _sbus_readl(const volatile void __iomem *addr)
{
	return *(__force volatile u32 *)addr;
}

static inline void _sbus_writeb(u8 b, volatile void __iomem *addr)
{
	*(__force volatile u8 *)addr = b;
}

static inline void _sbus_writew(u16 w, volatile void __iomem *addr)
{
	*(__force volatile u16 *)addr = w;
}

static inline void _sbus_writel(u32 l, volatile void __iomem *addr)
{
	*(__force volatile u32 *)addr = l;
}

/*
 * The only reason for #define's is to hide casts to unsigned long.
 */
#define sbus_readb(__addr)		_sbus_readb(__addr)
#define sbus_readw(__addr)		_sbus_readw(__addr)
#define sbus_readl(__addr)		_sbus_readl(__addr)
#define sbus_writeb(__b, __addr)	_sbus_writeb(__b, __addr)
#define sbus_writew(__w, __addr)	_sbus_writew(__w, __addr)
#define sbus_writel(__l, __addr)	_sbus_writel(__l, __addr)

static inline void sbus_memset_io(volatile void __iomem *__dst, int c, __kernel_size_t n)
{
	while(n--) {
		sbus_writeb(c, __dst);
		__dst++;
	}
}

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

#ifdef __KERNEL__

/*
 * Bus number may be embedded in the higher bits of the physical address.
 * This is why we have no bus number argument to ioremap().
 */
extern void __iomem *ioremap(unsigned long offset, unsigned long size);
#define ioremap_nocache(X,Y)	ioremap((X),(Y))
#define ioremap_wc(X,Y)		ioremap((X),(Y))
extern void iounmap(volatile void __iomem *addr);

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

/* Create a virtual mapping cookie for an IO port range */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *);

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
extern void pci_iounmap(struct pci_dev *dev, void __iomem *);

/*
 * At the moment, we do not use CMOS_READ anywhere outside of rtc.c,
 * so rtc_port is static in it. This should not change unless a new
 * hardware pops up.
 */
#define RTC_PORT(x)   (rtc_port + (x))
#define RTC_ALWAYS_BCD  0

static inline int sbus_can_dma_64bit(void)
{
	return 0; /* actually, sparc_cpu_model==sun4d */
}
static inline int sbus_can_burst64(void)
{
	return 0; /* actually, sparc_cpu_model==sun4d */
}
struct device;
extern void sbus_set_sbus64(struct device *, int);

#endif

#define __ARCH_HAS_NO_PAGE_ZERO_MAPPED		1

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif /* !(__SPARC_IO_H) */
