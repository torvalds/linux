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
#include <linux/pgtable.h>
#include <asm/mmiowb.h>
#include <asm/early_ioremap.h>

/*
 * MMIO access functions are separated out to break dependency cycles
 * when using {read,write}* fns in low-level headers
 */
#include <asm/mmio.h>

/*
 *  I/O port access constants.
 */
#ifdef CONFIG_MMU
#define IO_SPACE_LIMIT		(PCI_IO_SIZE - 1)
#define PCI_IOBASE		((void __iomem *)PCI_IO_START)
#endif /* CONFIG_MMU */

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
