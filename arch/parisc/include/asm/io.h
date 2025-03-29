/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/types.h>
#include <linux/pgtable.h>

#define virt_to_phys(a) ((unsigned long)__pa(a))
#define phys_to_virt(a) __va(a)

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
	"	rsm	%3,%0\n"
	"	ldbx	0(%2),%1\n"
	"	mtsm	%0\n"
	: "=&r" (flags), "=r" (ret) : "r" (addr), "i" (PSW_SM_D) );

	return ret;
}

static inline unsigned short gsc_readw(unsigned long addr)
{
	long flags;
	unsigned short ret;

	__asm__ __volatile__(
	"	rsm	%3,%0\n"
	"	ldhx	0(%2),%1\n"
	"	mtsm	%0\n"
	: "=&r" (flags), "=r" (ret) : "r" (addr), "i" (PSW_SM_D) );

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
	"	rsm	%3,%0\n"
	"	stbs	%1,0(%2)\n"
	"	mtsm	%0\n"
	: "=&r" (flags) :  "r" (val), "r" (addr), "i" (PSW_SM_D) );
}

static inline void gsc_writew(unsigned short val, unsigned long addr)
{
	long flags;
	__asm__ __volatile__(
	"	rsm	%3,%0\n"
	"	sths	%1,0(%2)\n"
	"	mtsm	%0\n"
	: "=&r" (flags) :  "r" (val), "r" (addr), "i" (PSW_SM_D) );
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
#define ioremap_prot ioremap_prot

#define _PAGE_IOREMAP (_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | \
		       _PAGE_ACCESSED | _PAGE_NO_CACHE)

#define ioremap_wc(addr, size)  \
	ioremap_prot((addr), (size), _PAGE_IOREMAP)

#define pci_iounmap			pci_iounmap

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
#define inb inb
#define inw inw
#define inl inl
#define outb outb
#define outw outw
#define outl outl
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
#define inb inb
#define inw inw
#define inl inl
#define outb(x, y)	({(void)(x); (void)(y); BUG(); 0;})
#define outw(x, y)	({(void)(x); (void)(y); BUG(); 0;})
#define outl(x, y)	({(void)(x); (void)(y); BUG(); 0;})
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
#define insb insb
#define insw insw
#define insl insl
#define outsb outsb
#define outsw outsw
#define outsl outsl

/* IO Port space is :      BBiiii   where BB is HBA number. */
#define IO_SPACE_LIMIT 0x00ffffff

/* PA machines have an MM I/O space from 0xf0000000-0xffffffff in 32
 * bit mode and from 0xfffffffff0000000-0xfffffffffffffff in 64 bit
 * mode (essentially just sign extending.  This macro takes in a 32
 * bit I/O address (still with the leading f) and outputs the correct
 * value for either 32 or 64 bit mode */
#define F_EXTEND(x) ((unsigned long)((x) | (0xffffffff00000000ULL)))

#ifdef CONFIG_64BIT
extern u64 ioread64(const void __iomem *addr);
extern u64 ioread64be(const void __iomem *addr);
#define ioread64 ioread64
#define ioread64be ioread64be

extern void iowrite64(u64 val, void __iomem *addr);
extern void iowrite64be(u64 val, void __iomem *addr);
#define iowrite64 iowrite64
#define iowrite64be iowrite64be
#endif

extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *);
#define ioport_map ioport_map
#define ioport_unmap ioport_unmap

extern unsigned int ioread8(const void __iomem *);
extern unsigned int ioread16(const void __iomem *);
extern unsigned int ioread16be(const void __iomem *);
extern unsigned int ioread32(const void __iomem *);
extern unsigned int ioread32be(const void __iomem *);
#define ioread8 ioread8
#define ioread16 ioread16
#define ioread32 ioread32
#define ioread16be ioread16be
#define ioread32be ioread32be

extern void iowrite8(u8, void __iomem *);
extern void iowrite16(u16, void __iomem *);
extern void iowrite16be(u16, void __iomem *);
extern void iowrite32(u32, void __iomem *);
extern void iowrite32be(u32, void __iomem *);
#define iowrite8 iowrite8
#define iowrite16 iowrite16
#define iowrite32 iowrite32
#define iowrite16be iowrite16be
#define iowrite32be iowrite32be

extern void ioread8_rep(const void __iomem *port, void *buf, unsigned long count);
extern void ioread16_rep(const void __iomem *port, void *buf, unsigned long count);
extern void ioread32_rep(const void __iomem *port, void *buf, unsigned long count);
#define ioread8_rep ioread8_rep
#define ioread16_rep ioread16_rep
#define ioread32_rep ioread32_rep

extern void iowrite8_rep(void __iomem *port, const void *buf, unsigned long count);
extern void iowrite16_rep(void __iomem *port, const void *buf, unsigned long count);
extern void iowrite32_rep(void __iomem *port, const void *buf, unsigned long count);
#define iowrite8_rep iowrite8_rep
#define iowrite16_rep iowrite16_rep
#define iowrite32_rep iowrite32_rep

extern int devmem_is_allowed(unsigned long pfn);

#include <asm-generic/io.h>

#endif
