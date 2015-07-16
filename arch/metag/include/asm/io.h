#ifndef _ASM_METAG_IO_H
#define _ASM_METAG_IO_H

#include <linux/types.h>
#include <asm/pgtable-bits.h>

#define IO_SPACE_LIMIT  0

#define page_to_bus page_to_phys
#define bus_to_page phys_to_page

/*
 * Generic I/O
 */

#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 ret;
	asm volatile("GETB %0,[%1]"
		     : "=da" (ret)
		     : "da" (addr)
		     : "memory");
	return ret;
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 ret;
	asm volatile("GETW %0,[%1]"
		     : "=da" (ret)
		     : "da" (addr)
		     : "memory");
	return ret;
}

#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 ret;
	asm volatile("GETD %0,[%1]"
		     : "=da" (ret)
		     : "da" (addr)
		     : "memory");
	return ret;
}

#define __raw_readq __raw_readq
static inline u64 __raw_readq(const volatile void __iomem *addr)
{
	u64 ret;
	asm volatile("GETL %0,%t0,[%1]"
		     : "=da" (ret)
		     : "da" (addr)
		     : "memory");
	return ret;
}

#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 b, volatile void __iomem *addr)
{
	asm volatile("SETB [%0],%1"
		     :
		     : "da" (addr),
		       "da" (b)
		     : "memory");
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 b, volatile void __iomem *addr)
{
	asm volatile("SETW [%0],%1"
		     :
		     : "da" (addr),
		       "da" (b)
		     : "memory");
}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 b, volatile void __iomem *addr)
{
	asm volatile("SETD [%0],%1"
		     :
		     : "da" (addr),
		       "da" (b)
		     : "memory");
}

#define __raw_writeq __raw_writeq
static inline void __raw_writeq(u64 b, volatile void __iomem *addr)
{
	asm volatile("SETL [%0],%1,%t1"
		     :
		     : "da" (addr),
		       "da" (b)
		     : "memory");
}

/*
 * The generic io.h can define all the other generic accessors
 */

#include <asm-generic/io.h>

/*
 * Despite being a 32bit architecture, Meta can do 64bit memory accesses
 * (assuming the bus supports it).
 */

#define readq	__raw_readq
#define writeq	__raw_writeq

/*
 * Meta specific I/O for accessing non-MMU areas.
 *
 * These can be provided with a physical address rather than an __iomem pointer
 * and should only be used by core architecture code for accessing fixed core
 * registers. Generic drivers should use ioremap and the generic I/O accessors.
 */

#define metag_in8(addr)		__raw_readb((volatile void __iomem *)(addr))
#define metag_in16(addr)	__raw_readw((volatile void __iomem *)(addr))
#define metag_in32(addr)	__raw_readl((volatile void __iomem *)(addr))
#define metag_in64(addr)	__raw_readq((volatile void __iomem *)(addr))

#define metag_out8(b, addr)	__raw_writeb(b, (volatile void __iomem *)(addr))
#define metag_out16(b, addr)	__raw_writew(b, (volatile void __iomem *)(addr))
#define metag_out32(b, addr)	__raw_writel(b, (volatile void __iomem *)(addr))
#define metag_out64(b, addr)	__raw_writeq(b, (volatile void __iomem *)(addr))

/*
 * io remapping functions
 */

extern void __iomem *__ioremap(unsigned long offset,
			       size_t size, unsigned long flags);
extern void __iounmap(void __iomem *addr);

/**
 *	ioremap		-	map bus memory into CPU space
 *	@offset:	bus address of the memory
 *	@size:		size of the resource to map
 *
 *	ioremap performs a platform specific sequence of operations to
 *	make bus memory CPU accessible via the readb/readw/readl/writeb/
 *	writew/writel functions and the other mmio helpers. The returned
 *	address is not guaranteed to be usable directly as a virtual
 *	address.
 */
#define ioremap(offset, size)                   \
	__ioremap((offset), (size), 0)

#define ioremap_nocache(offset, size)           \
	__ioremap((offset), (size), 0)

#define ioremap_cached(offset, size)            \
	__ioremap((offset), (size), _PAGE_CACHEABLE)

#define ioremap_wc(offset, size)                \
	__ioremap((offset), (size), _PAGE_WR_COMBINE)

#define ioremap_wt(offset, size)                \
	__ioremap((offset), (size), 0)

#define iounmap(addr)                           \
	__iounmap(addr)

#endif  /* _ASM_METAG_IO_H */
