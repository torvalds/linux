#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/types.h>
#include <asm/pgtable.h>

#define virt_to_phys(a) ((unsigned long)__pa(a))
#define phys_to_virt(a) __va(a)
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

static inline unsigned long isa_bus_to_virt(unsigned long addr) {
	BUG();
	return 0;
}

static inline unsigned long isa_virt_to_bus(void *addr) {
	BUG();
	return 0;
}

/*
 * Memory mapped I/O
 *
 * readX()/writeX() do byteswapping and take an ioremapped address
 * __raw_readX()/__raw_writeX() don't byteswap and take an ioremapped address.
 * gsc_*() don't byteswap and operate on physical addresses;
 *   eg dev->hpa or 0xfee00000.
 */

static inline unsigned char gsc_readb(unsigned long addr)
{
	long flags;
	unsigned char ret;

	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	ldbx	0(%2),%1\n"
	"	mtsm	%0\n"
	: "=&r" (flags), "=r" (ret) : "r" (addr) );

	return ret;
}

static inline unsigned short gsc_readw(unsigned long addr)
{
	long flags;
	unsigned short ret;

	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	ldhx	0(%2),%1\n"
	"	mtsm	%0\n"
	: "=&r" (flags), "=r" (ret) : "r" (addr) );

	return ret;
}

static inline unsigned int gsc_readl(unsigned long addr)
{
	u32 ret;

	__asm__ __volatile__(
	"	ldwax	0(%1),%0\n"
	: "=r" (ret) : "r" (addr) );

	return ret;
}

static inline unsigned long long gsc_readq(unsigned long addr)
{
	unsigned long long ret;

#ifdef CONFIG_64BIT
	__asm__ __volatile__(
	"	ldda	0(%1),%0\n"
	:  "=r" (ret) : "r" (addr) );
#else
	/* two reads may have side effects.. */
	ret = ((u64) gsc_readl(addr)) << 32;
	ret |= gsc_readl(addr+4);
#endif
	return ret;
}

static inline void gsc_writeb(unsigned char val, unsigned long addr)
{
	long flags;
	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	stbs	%1,0(%2)\n"
	"	mtsm	%0\n"
	: "=&r" (flags) :  "r" (val), "r" (addr) );
}

static inline void gsc_writew(unsigned short val, unsigned long addr)
{
	long flags;
	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	sths	%1,0(%2)\n"
	"	mtsm	%0\n"
	: "=&r" (flags) :  "r" (val), "r" (addr) );
}

static inline void gsc_writel(unsigned int val, unsigned long addr)
{
	__asm__ __volatile__(
	"	stwas	%0,0(%1)\n"
	: :  "r" (val), "r" (addr) );
}

static inline void gsc_writeq(unsigned long long val, unsigned long addr)
{
#ifdef CONFIG_64BIT
	__asm__ __volatile__(
	"	stda	%0,0(%1)\n"
	: :  "r" (val), "r" (addr) );
#else
	/* two writes may have side effects.. */
	gsc_writel(val >> 32, addr);
	gsc_writel(val, addr+4);
#endif
}

/*
 * The standard PCI ioremap interfaces
 */

extern void __iomem * __ioremap(unsigned long offset, unsigned long size, unsigned long flags);

/* Most machines react poorly to I/O-space being cacheable... Instead let's
 * define ioremap() in terms of ioremap_nocache().
 */
static inline void __iomem * ioremap(unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size, _PAGE_NO_CACHE);
}
#define ioremap_nocache(off, sz)	ioremap((off), (sz))

extern void iounmap(const volatile void __iomem *addr);

static inline unsigned char __raw_readb(const volatile void __iomem *addr)
{
	return (*(volatile unsigned char __force *) (addr));
}
static inline unsigned short __raw_readw(const volatile void __iomem *addr)
{
	return *(volatile unsigned short __force *) addr;
}
static inline unsigned int __raw_readl(const volatile void __iomem *addr)
{
	return *(volatile unsigned int __force *) addr;
}
static inline unsigned long long __raw_readq(const volatile void __iomem *addr)
{
	return *(volatile unsigned long long __force *) addr;
}

static inline void __raw_writeb(unsigned char b, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *) addr = b;
}
static inline void __raw_writew(unsigned short b, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *) addr = b;
}
static inline void __raw_writel(unsigned int b, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *) addr = b;
}
static inline void __raw_writeq(unsigned long long b, volatile void __iomem *addr)
{
	*(volatile unsigned long long __force *) addr = b;
}

static inline unsigned char readb(const volatile void __iomem *addr)
{
	return __raw_readb(addr);
}
static inline unsigned short readw(const volatile void __iomem *addr)
{
	return le16_to_cpu(__raw_readw(addr));
}
static inline unsigned int readl(const volatile void __iomem *addr)
{
	return le32_to_cpu(__raw_readl(addr));
}
static inline unsigned long long readq(const volatile void __iomem *addr)
{
	return le64_to_cpu(__raw_readq(addr));
}

static inline void writeb(unsigned char b, volatile void __iomem *addr)
{
	__raw_writeb(b, addr);
}
static inline void writew(unsigned short w, volatile void __iomem *addr)
{
	__raw_writew(cpu_to_le16(w), addr);
}
static inline void writel(unsigned int l, volatile void __iomem *addr)
{
	__raw_writel(cpu_to_le32(l), addr);
}
static inline void writeq(unsigned long long q, volatile void __iomem *addr)
{
	__raw_writeq(cpu_to_le64(q), addr);
}

#define	readb	readb
#define	readw	readw
#define	readl	readl
#define readq	readq
#define writeb	writeb
#define writew	writew
#define writel	writel
#define writeq	writeq

#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)
#define readq_relaxed(addr)	readq(addr)
#define writeb_relaxed(b, addr)	writeb(b, addr)
#define writew_relaxed(w, addr)	writew(w, addr)
#define writel_relaxed(l, addr)	writel(l, addr)
#define writeq_relaxed(q, addr)	writeq(q, addr)

#define mmiowb() do { } while (0)

void memset_io(volatile void __iomem *addr, unsigned char val, int count);
void memcpy_fromio(void *dst, const volatile void __iomem *src, int count);
void memcpy_toio(volatile void __iomem *dst, const void *src, int count);

/* Port-space IO */

#define inb_p inb
#define inw_p inw
#define inl_p inl
#define outb_p outb
#define outw_p outw
#define outl_p outl

extern unsigned char eisa_in8(unsigned short port);
extern unsigned short eisa_in16(unsigned short port);
extern unsigned int eisa_in32(unsigned short port);
extern void eisa_out8(unsigned char data, unsigned short port);
extern void eisa_out16(unsigned short data, unsigned short port);
extern void eisa_out32(unsigned int data, unsigned short port);

#if defined(CONFIG_PCI)
extern unsigned char inb(int addr);
extern unsigned short inw(int addr);
extern unsigned int inl(int addr);

extern void outb(unsigned char b, int addr);
extern void outw(unsigned short b, int addr);
extern void outl(unsigned int b, int addr);
#elif defined(CONFIG_EISA)
#define inb eisa_in8
#define inw eisa_in16
#define inl eisa_in32
#define outb eisa_out8
#define outw eisa_out16
#define outl eisa_out32
#else
static inline char inb(unsigned long addr)
{
	BUG();
	return -1;
}

static inline short inw(unsigned long addr)
{
	BUG();
	return -1;
}

static inline int inl(unsigned long addr)
{
	BUG();
	return -1;
}

#define outb(x, y)	BUG()
#define outw(x, y)	BUG()
#define outl(x, y)	BUG()
#endif

/*
 * String versions of in/out ops:
 */
extern void insb (unsigned long port, void *dst, unsigned long count);
extern void insw (unsigned long port, void *dst, unsigned long count);
extern void insl (unsigned long port, void *dst, unsigned long count);
extern void outsb (unsigned long port, const void *src, unsigned long count);
extern void outsw (unsigned long port, const void *src, unsigned long count);
extern void outsl (unsigned long port, const void *src, unsigned long count);


/* IO Port space is :      BBiiii   where BB is HBA number. */
#define IO_SPACE_LIMIT 0x00ffffff

/* PA machines have an MM I/O space from 0xf0000000-0xffffffff in 32
 * bit mode and from 0xfffffffff0000000-0xfffffffffffffff in 64 bit
 * mode (essentially just sign extending.  This macro takes in a 32
 * bit I/O address (still with the leading f) and outputs the correct
 * value for either 32 or 64 bit mode */
#define F_EXTEND(x) ((unsigned long)((x) | (0xffffffff00000000ULL)))

#include <asm-generic/iomap.h>

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
