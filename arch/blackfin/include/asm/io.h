#ifndef _BFIN_IO_H
#define _BFIN_IO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif
#include <linux/compiler.h>

/*
 * These are for ISA/PCI shared memory _only_ and should never be used
 * on any other type of memory, including Zorro memory. They are meant to
 * access the bus in the bus byte order which is little-endian!.
 *
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the bfin architecture, we just read/write the
 * memory location directly.
 */
#ifndef __ASSEMBLY__

static inline unsigned char readb(const volatile void __iomem *addr)
{
	unsigned int val;
	int tmp;

	__asm__ __volatile__ ("cli %1;\n\t"
			"NOP; NOP; SSYNC;\n\t"
			"%0 = b [%2] (z);\n\t"
			"sti %1;\n\t"
			: "=d"(val), "=d"(tmp): "a"(addr)
			);

	return (unsigned char) val;
}

static inline unsigned short readw(const volatile void __iomem *addr)
{
	unsigned int val;
	int tmp;

	__asm__ __volatile__ ("cli %1;\n\t"
			"NOP; NOP; SSYNC;\n\t"
			"%0 = w [%2] (z);\n\t"
			"sti %1;\n\t"
		      	: "=d"(val), "=d"(tmp): "a"(addr)
			);

	return (unsigned short) val;
}

static inline unsigned int readl(const volatile void __iomem *addr)
{
	unsigned int val;
	int tmp;

	__asm__ __volatile__ ("cli %1;\n\t"
			"NOP; NOP; SSYNC;\n\t"
			"%0 = [%2];\n\t"
			"sti %1;\n\t"
		      	: "=d"(val), "=d"(tmp): "a"(addr)
			);
	return val;
}

#endif /*  __ASSEMBLY__ */

#define writeb(b,addr) (void)((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) (void)((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) (void)((*(volatile unsigned int *) (addr)) = (b))

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

#define inb(addr)    readb(addr)
#define inw(addr)    readw(addr)
#define inl(addr)    readl(addr)
#define outb(x,addr) ((void) writeb(x,addr))
#define outw(x,addr) ((void) writew(x,addr))
#define outl(x,addr) ((void) writel(x,addr))

#define inb_p(addr)    inb(addr)
#define inw_p(addr)    inw(addr)
#define inl_p(addr)    inl(addr)
#define outb_p(x,addr) outb(x,addr)
#define outw_p(x,addr) outw(x,addr)
#define outl_p(x,addr) outl(x,addr)

#define ioread8_rep(a,d,c)	readsb(a,d,c)
#define ioread16_rep(a,d,c)	readsw(a,d,c)
#define ioread32_rep(a,d,c)	readsl(a,d,c)
#define iowrite8_rep(a,s,c)	writesb(a,s,c)
#define iowrite16_rep(a,s,c)	writesw(a,s,c)
#define iowrite32_rep(a,s,c)	writesl(a,s,c)

#define ioread8(X)			readb(X)
#define ioread16(X)			readw(X)
#define ioread32(X)			readl(X)
#define iowrite8(val,X)			writeb(val,X)
#define iowrite16(val,X)		writew(val,X)
#define iowrite32(val,X)		writel(val,X)

#define mmiowb() wmb()

#define IO_SPACE_LIMIT 0xffffffff

/* Values for nocacheflag and cmode */
#define IOMAP_NOCACHE_SER		1

#ifndef __ASSEMBLY__

extern void outsb(unsigned long port, const void *addr, unsigned long count);
extern void outsw(unsigned long port, const void *addr, unsigned long count);
extern void outsw_8(unsigned long port, const void *addr, unsigned long count);
extern void outsl(unsigned long port, const void *addr, unsigned long count);

extern void insb(unsigned long port, void *addr, unsigned long count);
extern void insw(unsigned long port, void *addr, unsigned long count);
extern void insw_8(unsigned long port, void *addr, unsigned long count);
extern void insl(unsigned long port, void *addr, unsigned long count);
extern void insl_16(unsigned long port, void *addr, unsigned long count);

extern void dma_outsb(unsigned long port, const void *addr, unsigned short count);
extern void dma_outsw(unsigned long port, const void *addr, unsigned short count);
extern void dma_outsl(unsigned long port, const void *addr, unsigned short count);

extern void dma_insb(unsigned long port, void *addr, unsigned short count);
extern void dma_insw(unsigned long port, void *addr, unsigned short count);
extern void dma_insl(unsigned long port, void *addr, unsigned short count);

static inline void readsl(const void __iomem *addr, void *buf, int len)
{
	insl((unsigned long)addr, buf, len);
}

static inline void readsw(const void __iomem *addr, void *buf, int len)
{
	insw((unsigned long)addr, buf, len);
}

static inline void readsb(const void __iomem *addr, void *buf, int len)
{
	insb((unsigned long)addr, buf, len);
}

static inline void writesl(const void __iomem *addr, const void *buf, int len)
{
	outsl((unsigned long)addr, buf, len);
}

static inline void writesw(const void __iomem *addr, const void *buf, int len)
{
	outsw((unsigned long)addr, buf, len);
}

static inline void writesb(const void __iomem *addr, const void *buf, int len)
{
	outsb((unsigned long)addr, buf, len);
}

/*
 * Map some physical address range into the kernel address space.
 */
static inline void __iomem *__ioremap(unsigned long physaddr, unsigned long size,
				int cacheflag)
{
	return (void __iomem *)physaddr;
}

/*
 * Unmap a ioremap()ed region again
 */
static inline void iounmap(void *addr)
{
}

/*
 * __iounmap unmaps nearly everything, so be careful
 * it doesn't free currently pointer/page tables anymore but it
 * wans't used anyway and might be added later.
 */
static inline void __iounmap(void *addr, unsigned long size)
{
}

/*
 * Set new cache mode for some kernel address space.
 * The caller must push data for that range itself, if such data may already
 * be in the cache.
 */
static inline void kernel_set_cachemode(void *addr, unsigned long size,
					int cmode)
{
}

static inline void __iomem *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
static inline void __iomem *ioremap_nocache(unsigned long physaddr,
					    unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}

extern void blkfin_inv_cache_all(void);

#endif

#define	ioport_map(port, nr)		((void __iomem*)(port))
#define	ioport_unmap(addr)

/* Pages to physical address... */
#define page_to_phys(page)      ((page - mem_map) << PAGE_SHIFT)
#define page_to_bus(page)       ((page - mem_map) << PAGE_SHIFT)

#define phys_to_virt(vaddr)	((void *) (vaddr))
#define virt_to_phys(vaddr)	((unsigned long) (vaddr))

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif				/* __KERNEL__ */

#endif				/* _BFIN_IO_H */
