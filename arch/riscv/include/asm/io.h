/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * {read,write}{b,w,l,q} based on arch/arm64/include/asm/io.h
 *   which was based on arch/arm/include/io.h
 *
 * Copyright (C) 1996-2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2014 Regents of the University of California
 */

#ifndef _ASM_RISCV_IO_H
#define _ASM_RISCV_IO_H

#include <linux/types.h>
#include <asm/mmiowb.h>
#include <asm/pgtable.h>

extern void __iomem *ioremap(phys_addr_t offset, unsigned long size);

/*
 * The RISC-V ISA doesn't yet specify how to query or modify PMAs, so we can't
 * change the properties of memory regions.  This should be fixed by the
 * upcoming platform spec.
 */
#define ioremap_nocache(addr, size) ioremap((addr), (size))
#define ioremap_wc(addr, size) ioremap((addr), (size))
#define ioremap_wt(addr, size) ioremap((addr), (size))

extern void iounmap(volatile void __iomem *addr);

/* Generic IO read/write.  These perform native-endian accesses. */
#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 val, volatile void __iomem *addr)
{
	asm volatile("sb %0, 0(%1)" : : "r" (val), "r" (addr));
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 val, volatile void __iomem *addr)
{
	asm volatile("sh %0, 0(%1)" : : "r" (val), "r" (addr));
}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 val, volatile void __iomem *addr)
{
	asm volatile("sw %0, 0(%1)" : : "r" (val), "r" (addr));
}

#ifdef CONFIG_64BIT
#define __raw_writeq __raw_writeq
static inline void __raw_writeq(u64 val, volatile void __iomem *addr)
{
	asm volatile("sd %0, 0(%1)" : : "r" (val), "r" (addr));
}
#endif

#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 val;

	asm volatile("lb %0, 0(%1)" : "=r" (val) : "r" (addr));
	return val;
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 val;

	asm volatile("lh %0, 0(%1)" : "=r" (val) : "r" (addr));
	return val;
}

#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 val;

	asm volatile("lw %0, 0(%1)" : "=r" (val) : "r" (addr));
	return val;
}

#ifdef CONFIG_64BIT
#define __raw_readq __raw_readq
static inline u64 __raw_readq(const volatile void __iomem *addr)
{
	u64 val;

	asm volatile("ld %0, 0(%1)" : "=r" (val) : "r" (addr));
	return val;
}
#endif

/*
 * Unordered I/O memory access primitives.  These are even more relaxed than
 * the relaxed versions, as they don't even order accesses between successive
 * operations to the I/O regions.
 */
#define readb_cpu(c)		({ u8  __r = __raw_readb(c); __r; })
#define readw_cpu(c)		({ u16 __r = le16_to_cpu((__force __le16)__raw_readw(c)); __r; })
#define readl_cpu(c)		({ u32 __r = le32_to_cpu((__force __le32)__raw_readl(c)); __r; })

#define writeb_cpu(v,c)		((void)__raw_writeb((v),(c)))
#define writew_cpu(v,c)		((void)__raw_writew((__force u16)cpu_to_le16(v),(c)))
#define writel_cpu(v,c)		((void)__raw_writel((__force u32)cpu_to_le32(v),(c)))

#ifdef CONFIG_64BIT
#define readq_cpu(c)		({ u64 __r = le64_to_cpu((__force __le64)__raw_readq(c)); __r; })
#define writeq_cpu(v,c)		((void)__raw_writeq((__force u64)cpu_to_le64(v),(c)))
#endif

/*
 * Relaxed I/O memory access primitives. These follow the Device memory
 * ordering rules but do not guarantee any ordering relative to Normal memory
 * accesses.  These are defined to order the indicated access (either a read or
 * write) with all other I/O memory accesses. Since the platform specification
 * defines that all I/O regions are strongly ordered on channel 2, no explicit
 * fences are required to enforce this ordering.
 */
/* FIXME: These are now the same as asm-generic */
#define __io_rbr()		do {} while (0)
#define __io_rar()		do {} while (0)
#define __io_rbw()		do {} while (0)
#define __io_raw()		do {} while (0)

#define readb_relaxed(c)	({ u8  __v; __io_rbr(); __v = readb_cpu(c); __io_rar(); __v; })
#define readw_relaxed(c)	({ u16 __v; __io_rbr(); __v = readw_cpu(c); __io_rar(); __v; })
#define readl_relaxed(c)	({ u32 __v; __io_rbr(); __v = readl_cpu(c); __io_rar(); __v; })

#define writeb_relaxed(v,c)	({ __io_rbw(); writeb_cpu((v),(c)); __io_raw(); })
#define writew_relaxed(v,c)	({ __io_rbw(); writew_cpu((v),(c)); __io_raw(); })
#define writel_relaxed(v,c)	({ __io_rbw(); writel_cpu((v),(c)); __io_raw(); })

#ifdef CONFIG_64BIT
#define readq_relaxed(c)	({ u64 __v; __io_rbr(); __v = readq_cpu(c); __io_rar(); __v; })
#define writeq_relaxed(v,c)	({ __io_rbw(); writeq_cpu((v),(c)); __io_raw(); })
#endif

/*
 * I/O memory access primitives. Reads are ordered relative to any
 * following Normal memory access. Writes are ordered relative to any prior
 * Normal memory access.  The memory barriers here are necessary as RISC-V
 * doesn't define any ordering between the memory space and the I/O space.
 */
#define __io_br()	do {} while (0)
#define __io_ar(v)	__asm__ __volatile__ ("fence i,r" : : : "memory");
#define __io_bw()	__asm__ __volatile__ ("fence w,o" : : : "memory");
#define __io_aw()	mmiowb_set_pending()

#define readb(c)	({ u8  __v; __io_br(); __v = readb_cpu(c); __io_ar(__v); __v; })
#define readw(c)	({ u16 __v; __io_br(); __v = readw_cpu(c); __io_ar(__v); __v; })
#define readl(c)	({ u32 __v; __io_br(); __v = readl_cpu(c); __io_ar(__v); __v; })

#define writeb(v,c)	({ __io_bw(); writeb_cpu((v),(c)); __io_aw(); })
#define writew(v,c)	({ __io_bw(); writew_cpu((v),(c)); __io_aw(); })
#define writel(v,c)	({ __io_bw(); writel_cpu((v),(c)); __io_aw(); })

#ifdef CONFIG_64BIT
#define readq(c)	({ u64 __v; __io_br(); __v = readq_cpu(c); __io_ar(__v); __v; })
#define writeq(v,c)	({ __io_bw(); writeq_cpu((v),(c)); __io_aw(); })
#endif

/*
 *  I/O port access constants.
 */
#define IO_SPACE_LIMIT		(PCI_IO_SIZE - 1)
#define PCI_IOBASE		((void __iomem *)PCI_IO_START)

/*
 * Emulation routines for the port-mapped IO space used by some PCI drivers.
 * These are defined as being "fully synchronous", but also "not guaranteed to
 * be fully ordered with respect to other memory and I/O operations".  We're
 * going to be on the safe side here and just make them:
 *  - Fully ordered WRT each other, by bracketing them with two fences.  The
 *    outer set contains both I/O so inX is ordered with outX, while the inner just
 *    needs the type of the access (I for inX and O for outX).
 *  - Ordered in the same manner as readX/writeX WRT memory by subsuming their
 *    fences.
 *  - Ordered WRT timer reads, so udelay and friends don't get elided by the
 *    implementation.
 * Note that there is no way to actually enforce that outX is a non-posted
 * operation on RISC-V, but hopefully the timer ordering constraint is
 * sufficient to ensure this works sanely on controllers that support I/O
 * writes.
 */
#define __io_pbr()	__asm__ __volatile__ ("fence io,i"  : : : "memory");
#define __io_par(v)	__asm__ __volatile__ ("fence i,ior" : : : "memory");
#define __io_pbw()	__asm__ __volatile__ ("fence iow,o" : : : "memory");
#define __io_paw()	__asm__ __volatile__ ("fence o,io"  : : : "memory");

#define inb(c)		({ u8  __v; __io_pbr(); __v = readb_cpu((void*)(PCI_IOBASE + (c))); __io_par(__v); __v; })
#define inw(c)		({ u16 __v; __io_pbr(); __v = readw_cpu((void*)(PCI_IOBASE + (c))); __io_par(__v); __v; })
#define inl(c)		({ u32 __v; __io_pbr(); __v = readl_cpu((void*)(PCI_IOBASE + (c))); __io_par(__v); __v; })

#define outb(v,c)	({ __io_pbw(); writeb_cpu((v),(void*)(PCI_IOBASE + (c))); __io_paw(); })
#define outw(v,c)	({ __io_pbw(); writew_cpu((v),(void*)(PCI_IOBASE + (c))); __io_paw(); })
#define outl(v,c)	({ __io_pbw(); writel_cpu((v),(void*)(PCI_IOBASE + (c))); __io_paw(); })

#ifdef CONFIG_64BIT
#define inq(c)		({ u64 __v; __io_pbr(); __v = readq_cpu((void*)(c)); __io_par(__v); __v; })
#define outq(v,c)	({ __io_pbw(); writeq_cpu((v),(void*)(c)); __io_paw(); })
#endif

/*
 * Accesses from a single hart to a single I/O address must be ordered.  This
 * allows us to use the raw read macros, but we still need to fence before and
 * after the block to ensure ordering WRT other macros.  These are defined to
 * perform host-endian accesses so we use __raw instead of __cpu.
 */
#define __io_reads_ins(port, ctype, len, bfence, afence)			\
	static inline void __ ## port ## len(const volatile void __iomem *addr,	\
					     void *buffer,			\
					     unsigned int count)		\
	{									\
		bfence;								\
		if (count) {							\
			ctype *buf = buffer;					\
										\
			do {							\
				ctype x = __raw_read ## len(addr);		\
				*buf++ = x;					\
			} while (--count);					\
		}								\
		afence;								\
	}

#define __io_writes_outs(port, ctype, len, bfence, afence)			\
	static inline void __ ## port ## len(volatile void __iomem *addr,	\
					     const void *buffer,		\
					     unsigned int count)		\
	{									\
		bfence;								\
		if (count) {							\
			const ctype *buf = buffer;				\
										\
			do {							\
				__raw_write ## len(*buf++, addr);		\
			} while (--count);					\
		}								\
		afence;								\
	}

__io_reads_ins(reads,  u8, b, __io_br(), __io_ar(addr))
__io_reads_ins(reads, u16, w, __io_br(), __io_ar(addr))
__io_reads_ins(reads, u32, l, __io_br(), __io_ar(addr))
#define readsb(addr, buffer, count) __readsb(addr, buffer, count)
#define readsw(addr, buffer, count) __readsw(addr, buffer, count)
#define readsl(addr, buffer, count) __readsl(addr, buffer, count)

__io_reads_ins(ins,  u8, b, __io_pbr(), __io_par(addr))
__io_reads_ins(ins, u16, w, __io_pbr(), __io_par(addr))
__io_reads_ins(ins, u32, l, __io_pbr(), __io_par(addr))
#define insb(addr, buffer, count) __insb((void __iomem *)(long)addr, buffer, count)
#define insw(addr, buffer, count) __insw((void __iomem *)(long)addr, buffer, count)
#define insl(addr, buffer, count) __insl((void __iomem *)(long)addr, buffer, count)

__io_writes_outs(writes,  u8, b, __io_bw(), __io_aw())
__io_writes_outs(writes, u16, w, __io_bw(), __io_aw())
__io_writes_outs(writes, u32, l, __io_bw(), __io_aw())
#define writesb(addr, buffer, count) __writesb(addr, buffer, count)
#define writesw(addr, buffer, count) __writesw(addr, buffer, count)
#define writesl(addr, buffer, count) __writesl(addr, buffer, count)

__io_writes_outs(outs,  u8, b, __io_pbw(), __io_paw())
__io_writes_outs(outs, u16, w, __io_pbw(), __io_paw())
__io_writes_outs(outs, u32, l, __io_pbw(), __io_paw())
#define outsb(addr, buffer, count) __outsb((void __iomem *)(long)addr, buffer, count)
#define outsw(addr, buffer, count) __outsw((void __iomem *)(long)addr, buffer, count)
#define outsl(addr, buffer, count) __outsl((void __iomem *)(long)addr, buffer, count)

#ifdef CONFIG_64BIT
__io_reads_ins(reads, u64, q, __io_br(), __io_ar(addr))
#define readsq(addr, buffer, count) __readsq(addr, buffer, count)

__io_reads_ins(ins, u64, q, __io_pbr(), __io_par(addr))
#define insq(addr, buffer, count) __insq((void __iomem *)addr, buffer, count)

__io_writes_outs(writes, u64, q, __io_bw(), __io_aw())
#define writesq(addr, buffer, count) __writesq(addr, buffer, count)

__io_writes_outs(outs, u64, q, __io_pbr(), __io_paw())
#define outsq(addr, buffer, count) __outsq((void __iomem *)addr, buffer, count)
#endif

#include <asm-generic/io.h>

#endif /* _ASM_RISCV_IO_H */
