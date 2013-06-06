/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_IO_H
#define _ASM_NIOS2_IO_H

#include <asm/pgtable-bits.h>

#define IO_SPACE_LIMIT 0xffffffff

#ifndef CONFIG_CC_OPTIMIZE_FOR_SIZE
# define __IO_USE_DUFFS
#endif

#ifdef __IO_USE_DUFFS

/* Use "Duff's Device" to unroll the loops. */
#define __IO_OUT_LOOP(a, b, l)				\
	do {						\
		if (l > 0) {				\
			int _n = (l + 7) / 8;		\
			switch (l % 8) {		\
			case 0:				\
				do {			\
					*a = *b++;	\
			case 7:				\
					*a = *b++;	\
			case 6:				\
					*a = *b++;	\
			case 5:				\
					*a = *b++;	\
			case 4:				\
					*a = *b++;	\
			case 3:				\
					*a = *b++;	\
			case 2:				\
					*a = *b++;	\
			case 1:				\
					*a = *b++;	\
				} while (--_n > 0);	\
			}				\
		}					\
	} while (0)

#define __IO_IN_LOOP(a, b, l)				\
	do {						\
		if (l > 0) {				\
			int _n = (l + 7) / 8;		\
			switch (l % 8) {		\
			case 0:				\
				do {			\
					*b++ = *a;	\
			case 7:				\
					*b++ = *a;	\
			case 6:				\
					*b++ = *a;	\
			case 5:				\
					*b++ = *a;	\
			case 4:				\
					*b++ = *a;	\
			case 3:				\
					*b++ = *a;	\
			case 2:				\
					*b++ = *a;	\
			case 1:				\
					*b++ = *a;	\
				} while (--_n > 0);	\
			}				\
		}					\
	} while (0)

#else /* __IO_USE_DUFFS */

/* Use simple loops. */
#define __IO_OUT_LOOP(a, b, l)				\
	do {						\
		while (l--)				\
			*a = *b++;			\
	} while (0)

#define __IO_IN_LOOP(a, b, l)				\
	do {						\
		while (l--)				\
			*b++ = *a;			\
	} while (0)

#endif /* __IO_USE_DUFFS */

static inline void io_outsb(unsigned int addr, const void *buf, int len)
{
	volatile unsigned char *ap = (volatile unsigned char *)addr;
	unsigned char *bp = (unsigned char *)buf;
	__IO_OUT_LOOP(ap, bp, len);
}

static inline void io_outsw(unsigned int addr, const void *buf, int len)
{
	volatile unsigned short *ap = (volatile unsigned short *)addr;
	unsigned short *bp = (unsigned short *)buf;
	__IO_OUT_LOOP(ap, bp, len);
}

static inline void io_outsl(unsigned int addr, const void *buf, int len)
{
	volatile unsigned int *ap = (volatile unsigned int *)addr;
	unsigned int *bp = (unsigned int *)buf;
	__IO_OUT_LOOP(ap, bp, len);
}

static inline void io_insb(unsigned int addr, void *buf, int len)
{
	volatile unsigned char *ap = (volatile unsigned char *)addr;
	unsigned char *bp = (unsigned char *)buf;
	__IO_IN_LOOP(ap, bp, len);
}

static inline void io_insw(unsigned int addr, void *buf, int len)
{
	volatile unsigned short *ap = (volatile unsigned short *)addr;
	unsigned short *bp = (unsigned short *)buf;
	__IO_IN_LOOP(ap, bp, len);
}

static inline void io_insl(unsigned int addr, void *buf, int len)
{
	volatile unsigned int *ap = (volatile unsigned int *)addr;
	unsigned int *bp = (unsigned int *)buf;
	__IO_IN_LOOP(ap, bp, len);
}

#undef __IO_OUT_LOOP
#undef __IO_IN_LOOP
#undef __IO_USE_DUFFS

#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)

#define outsb(a, b, l)		io_outsb(a, b, l)
#define outsw(a, b, l)		io_outsw(a, b, l)
#define outsl(a, b, l)		io_outsl(a, b, l)

#define insb(a, b, l)		io_insb(a, b, l)
#define insw(a, b, l)		io_insw(a, b, l)
#define insl(a, b, l)		io_insl(a, b, l)

#include <asm-generic/io.h>

/*
 *	make the short names macros so specific devices
 *	can override them as required
 */
#define inb(addr)		readb(addr)
#define inw(addr)		readw(addr)
#define inl(addr)		readl(addr)
#define outb(x, addr)		((void) writeb(x, addr))
#define outw(x, addr)		((void) writew(x, addr))
#define outl(x, addr)		((void) writel(x, addr))

#ifdef CONFIG_MMU

extern void __iomem *__ioremap(unsigned long physaddr, unsigned long size,
			unsigned long cacheflag);
extern void __iounmap(void __iomem *addr);

#else

static inline void __iomem *__ioremap(unsigned long physaddr,
					unsigned long size,
					unsigned long cacheflag)
{
	if (cacheflag & _PAGE_CACHED)
		return (void __iomem *)(physaddr & ~CONFIG_IO_REGION_BASE);
	else
		return (void __iomem *)(physaddr | CONFIG_IO_REGION_BASE);
}

#define __iounmap(addr)		do {} while (0)

#endif /* CONFIG_MMU */

static inline void __iomem *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, 0);
}

static inline void __iomem *ioremap_nocache(unsigned long physaddr,
						unsigned long size)
{
	return __ioremap(physaddr, size, 0);
}

static inline void __iomem *ioremap_writethrough(unsigned long physaddr,
						unsigned long size)
{
	return __ioremap(physaddr, size, 0);
}

static inline void __iomem *ioremap_fullcache(unsigned long physaddr,
						unsigned long size)
{
	return __ioremap(physaddr, size, _PAGE_CACHED);
}

static inline void iounmap(void __iomem *addr)
{
	__iounmap(addr);
}

/* Pages to physical address... */
#ifdef CONFIG_MMU
# define page_to_phys(page)	virt_to_phys(page_to_virt(page))
# define page_to_bus(page)	page_to_virt(page)
#else
# define page_to_phys(page)	((page - mem_map) << PAGE_SHIFT)
# define page_to_bus(page)	((page - mem_map) << PAGE_SHIFT)
#endif /* CONFIG_MMU */

/* Macros used for converting between virtual and physical mappings. */
#ifdef CONFIG_MMU
# define phys_to_virt(vaddr)	\
	((void *)((unsigned long)vaddr + PAGE_OFFSET - PHYS_OFFSET))
# define virt_to_phys(vaddr)	\
	((unsigned long)((unsigned long)vaddr - PAGE_OFFSET + PHYS_OFFSET))
#else
# define phys_to_virt(vaddr)	((void *)(vaddr))
# define virt_to_phys(vaddr)	((unsigned long)(vaddr))
#endif /* CONFIG_MMU */

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

#define ioport_map(port, nr)	ioremap(port, nr)
#define ioport_unmap(port)	iounmap(port)

/* Macros used for smc91x.c driver */
#define readsb(p, d, l)		insb(p, d, l)
#define readsw(p, d, l)		insw(p, d, l)
#define readsl(p, d, l)		insl(p, d, l)
#define writesb(p, d, l)	outsb(p, d, l)
#define writesw(p, d, l)	outsw(p, d, l)
#define writesl(p, d, l)	outsl(p, d, l)

#endif /* _ASM_NIOS2_IO_H */
