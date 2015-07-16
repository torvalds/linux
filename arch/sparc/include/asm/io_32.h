#ifndef __SPARC_IO_H
#define __SPARC_IO_H

#include <linux/kernel.h>
#include <linux/ioport.h>  /* struct resource */

#define IO_SPACE_LIMIT 0xffffffff

#define memset_io(d,c,sz)     _memset_io(d,c,sz)
#define memcpy_fromio(d,s,sz) _memcpy_fromio(d,s,sz)
#define memcpy_toio(d,s,sz)   _memcpy_toio(d,s,sz)

#include <asm-generic/io.h>

static inline void _memset_io(volatile void __iomem *dst,
                              int c, __kernel_size_t n)
{
	volatile void __iomem *d = dst;

	while (n--) {
		writeb(c, d);
		d++;
	}
}

static inline void _memcpy_fromio(void *dst, const volatile void __iomem *src,
                                  __kernel_size_t n)
{
	char *d = dst;

	while (n--) {
		char tmp = readb(src);
		*d++ = tmp;
		src++;
	}
}

static inline void _memcpy_toio(volatile void __iomem *dst, const void *src,
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

/*
 * SBus accessors.
 *
 * SBus has only one, memory mapped, I/O space.
 * We do not need to flip bytes for SBus of course.
 */
static inline u8 sbus_readb(const volatile void __iomem *addr)
{
	return *(__force volatile u8 *)addr;
}

static inline u16 sbus_readw(const volatile void __iomem *addr)
{
	return *(__force volatile u16 *)addr;
}

static inline u32 sbus_readl(const volatile void __iomem *addr)
{
	return *(__force volatile u32 *)addr;
}

static inline void sbus_writeb(u8 b, volatile void __iomem *addr)
{
	*(__force volatile u8 *)addr = b;
}

static inline void sbus_writew(u16 w, volatile void __iomem *addr)
{
	*(__force volatile u16 *)addr = w;
}

static inline void sbus_writel(u32 l, volatile void __iomem *addr)
{
	*(__force volatile u32 *)addr = l;
}

static inline void sbus_memset_io(volatile void __iomem *__dst, int c,
                                  __kernel_size_t n)
{
	while(n--) {
		sbus_writeb(c, __dst);
		__dst++;
	}
}

static inline void sbus_memcpy_fromio(void *dst,
                                      const volatile void __iomem *src,
                                      __kernel_size_t n)
{
	char *d = dst;

	while (n--) {
		char tmp = sbus_readb(src);
		*d++ = tmp;
		src++;
	}
}

static inline void sbus_memcpy_toio(volatile void __iomem *dst,
                                    const void *src,
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

#ifdef __KERNEL__

/*
 * Bus number may be embedded in the higher bits of the physical address.
 * This is why we have no bus number argument to ioremap().
 */
void __iomem *ioremap(unsigned long offset, unsigned long size);
#define ioremap_nocache(X,Y)	ioremap((X),(Y))
#define ioremap_wc(X,Y)		ioremap((X),(Y))
#define ioremap_wt(X,Y)		ioremap((X),(Y))
void iounmap(volatile void __iomem *addr);

/* Create a virtual mapping cookie for an IO port range */
void __iomem *ioport_map(unsigned long port, unsigned int nr);
void ioport_unmap(void __iomem *);

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
void pci_iounmap(struct pci_dev *dev, void __iomem *);



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
void sbus_set_sbus64(struct device *, int);

#endif

#define __ARCH_HAS_NO_PAGE_ZERO_MAPPED		1


#endif /* !(__SPARC_IO_H) */
