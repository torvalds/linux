#ifndef _ASM_POWERPC_IO_H
#define _ASM_POWERPC_IO_H
#ifdef __KERNEL__

#define ARCH_HAS_IOREMAP_WC
#ifdef CONFIG_PPC32
#define ARCH_HAS_IOREMAP_WT
#endif

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* Check of existence of legacy devices */
extern int check_legacy_ioport(unsigned long base_port);
#define I8042_DATA_REG	0x60
#define FDC_BASE	0x3f0

#if defined(CONFIG_PPC64) && defined(CONFIG_PCI)
extern struct pci_dev *isa_bridge_pcidev;
/*
 * has legacy ISA devices ?
 */
#define arch_has_dev_port()	(isa_bridge_pcidev != NULL || isa_io_special)
#endif

#include <linux/device.h>
#include <linux/compiler.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/byteorder.h>
#include <asm/synch.h>
#include <asm/delay.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/pgtable.h>

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#endif

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define SLOW_DOWN_IO

/* 32 bits uses slightly different variables for the various IO
 * bases. Most of this file only uses _IO_BASE though which we
 * define properly based on the platform
 */
#ifndef CONFIG_PCI
#define _IO_BASE	0
#define _ISA_MEM_BASE	0
#define PCI_DRAM_OFFSET 0
#elif defined(CONFIG_PPC32)
#define _IO_BASE	isa_io_base
#define _ISA_MEM_BASE	isa_mem_base
#define PCI_DRAM_OFFSET	pci_dram_offset
#else
#define _IO_BASE	pci_io_base
#define _ISA_MEM_BASE	isa_mem_base
#define PCI_DRAM_OFFSET	0
#endif

extern unsigned long isa_io_base;
extern unsigned long pci_io_base;
extern unsigned long pci_dram_offset;

extern resource_size_t isa_mem_base;

/* Boolean set by platform if PIO accesses are suppored while _IO_BASE
 * is not set or addresses cannot be translated to MMIO. This is typically
 * set when the platform supports "special" PIO accesses via a non memory
 * mapped mechanism, and allows things like the early udbg UART code to
 * function.
 */
extern bool isa_io_special;

#ifdef CONFIG_PPC32
#if defined(CONFIG_PPC_INDIRECT_PIO) || defined(CONFIG_PPC_INDIRECT_MMIO)
#error CONFIG_PPC_INDIRECT_{PIO,MMIO} are not yet supported on 32 bits
#endif
#endif

/*
 *
 * Low level MMIO accessors
 *
 * This provides the non-bus specific accessors to MMIO. Those are PowerPC
 * specific and thus shouldn't be used in generic code. The accessors
 * provided here are:
 *
 *	in_8, in_le16, in_be16, in_le32, in_be32, in_le64, in_be64
 *	out_8, out_le16, out_be16, out_le32, out_be32, out_le64, out_be64
 *	_insb, _insw_ns, _insl_ns, _outsb, _outsw_ns, _outsl_ns
 *
 * Those operate directly on a kernel virtual address. Note that the prototype
 * for the out_* accessors has the arguments in opposite order from the usual
 * linux PCI accessors. Unlike those, they take the address first and the value
 * next.
 *
 * Note: I might drop the _ns suffix on the stream operations soon as it is
 * simply normal for stream operations to not swap in the first place.
 *
 */

#ifdef CONFIG_PPC64
#define IO_SET_SYNC_FLAG()	do { local_paca->io_sync = 1; } while(0)
#else
#define IO_SET_SYNC_FLAG()
#endif

#define DEF_MMIO_IN_X(name, size, insn)				\
static inline u##size name(const volatile u##size __iomem *addr)	\
{									\
	u##size ret;							\
	__asm__ __volatile__("sync;"#insn" %0,%y1;twi 0,%0,0;isync"	\
		: "=r" (ret) : "Z" (*addr) : "memory");			\
	return ret;							\
}

#define DEF_MMIO_OUT_X(name, size, insn)				\
static inline void name(volatile u##size __iomem *addr, u##size val)	\
{									\
	__asm__ __volatile__("sync;"#insn" %1,%y0"			\
		: "=Z" (*addr) : "r" (val) : "memory");			\
	IO_SET_SYNC_FLAG();						\
}

#define DEF_MMIO_IN_D(name, size, insn)				\
static inline u##size name(const volatile u##size __iomem *addr)	\
{									\
	u##size ret;							\
	__asm__ __volatile__("sync;"#insn"%U1%X1 %0,%1;twi 0,%0,0;isync"\
		: "=r" (ret) : "m" (*addr) : "memory");			\
	return ret;							\
}

#define DEF_MMIO_OUT_D(name, size, insn)				\
static inline void name(volatile u##size __iomem *addr, u##size val)	\
{									\
	__asm__ __volatile__("sync;"#insn"%U0%X0 %1,%0"			\
		: "=m" (*addr) : "r" (val) : "memory");			\
	IO_SET_SYNC_FLAG();						\
}

DEF_MMIO_IN_D(in_8,     8, lbz);
DEF_MMIO_OUT_D(out_8,   8, stb);

#ifdef __BIG_ENDIAN__
DEF_MMIO_IN_D(in_be16, 16, lhz);
DEF_MMIO_IN_D(in_be32, 32, lwz);
DEF_MMIO_IN_X(in_le16, 16, lhbrx);
DEF_MMIO_IN_X(in_le32, 32, lwbrx);

DEF_MMIO_OUT_D(out_be16, 16, sth);
DEF_MMIO_OUT_D(out_be32, 32, stw);
DEF_MMIO_OUT_X(out_le16, 16, sthbrx);
DEF_MMIO_OUT_X(out_le32, 32, stwbrx);
#else
DEF_MMIO_IN_X(in_be16, 16, lhbrx);
DEF_MMIO_IN_X(in_be32, 32, lwbrx);
DEF_MMIO_IN_D(in_le16, 16, lhz);
DEF_MMIO_IN_D(in_le32, 32, lwz);

DEF_MMIO_OUT_X(out_be16, 16, sthbrx);
DEF_MMIO_OUT_X(out_be32, 32, stwbrx);
DEF_MMIO_OUT_D(out_le16, 16, sth);
DEF_MMIO_OUT_D(out_le32, 32, stw);

#endif /* __BIG_ENDIAN */

#ifdef __powerpc64__

#ifdef __BIG_ENDIAN__
DEF_MMIO_OUT_D(out_be64, 64, std);
DEF_MMIO_IN_D(in_be64, 64, ld);

/* There is no asm instructions for 64 bits reverse loads and stores */
static inline u64 in_le64(const volatile u64 __iomem *addr)
{
	return swab64(in_be64(addr));
}

static inline void out_le64(volatile u64 __iomem *addr, u64 val)
{
	out_be64(addr, swab64(val));
}
#else
DEF_MMIO_OUT_D(out_le64, 64, std);
DEF_MMIO_IN_D(in_le64, 64, ld);

/* There is no asm instructions for 64 bits reverse loads and stores */
static inline u64 in_be64(const volatile u64 __iomem *addr)
{
	return swab64(in_le64(addr));
}

static inline void out_be64(volatile u64 __iomem *addr, u64 val)
{
	out_le64(addr, swab64(val));
}

#endif
#endif /* __powerpc64__ */

/*
 * Low level IO stream instructions are defined out of line for now
 */
extern void _insb(const volatile u8 __iomem *addr, void *buf, long count);
extern void _outsb(volatile u8 __iomem *addr,const void *buf,long count);
extern void _insw_ns(const volatile u16 __iomem *addr, void *buf, long count);
extern void _outsw_ns(volatile u16 __iomem *addr, const void *buf, long count);
extern void _insl_ns(const volatile u32 __iomem *addr, void *buf, long count);
extern void _outsl_ns(volatile u32 __iomem *addr, const void *buf, long count);

/* The _ns naming is historical and will be removed. For now, just #define
 * the non _ns equivalent names
 */
#define _insw	_insw_ns
#define _insl	_insl_ns
#define _outsw	_outsw_ns
#define _outsl	_outsl_ns


/*
 * memset_io, memcpy_toio, memcpy_fromio base implementations are out of line
 */

extern void _memset_io(volatile void __iomem *addr, int c, unsigned long n);
extern void _memcpy_fromio(void *dest, const volatile void __iomem *src,
			   unsigned long n);
extern void _memcpy_toio(volatile void __iomem *dest, const void *src,
			 unsigned long n);

/*
 *
 * PCI and standard ISA accessors
 *
 * Those are globally defined linux accessors for devices on PCI or ISA
 * busses. They follow the Linux defined semantics. The current implementation
 * for PowerPC is as close as possible to the x86 version of these, and thus
 * provides fairly heavy weight barriers for the non-raw versions
 *
 * In addition, they support a hook mechanism when CONFIG_PPC_INDIRECT_MMIO
 * or CONFIG_PPC_INDIRECT_PIO are set allowing the platform to provide its
 * own implementation of some or all of the accessors.
 */

/*
 * Include the EEH definitions when EEH is enabled only so they don't get
 * in the way when building for 32 bits
 */
#ifdef CONFIG_EEH
#include <asm/eeh.h>
#endif

/* Shortcut to the MMIO argument pointer */
#define PCI_IO_ADDR	volatile void __iomem *

/* Indirect IO address tokens:
 *
 * When CONFIG_PPC_INDIRECT_MMIO is set, the platform can provide hooks
 * on all MMIOs. (Note that this is all 64 bits only for now)
 *
 * To help platforms who may need to differentiate MMIO addresses in
 * their hooks, a bitfield is reserved for use by the platform near the
 * top of MMIO addresses (not PIO, those have to cope the hard way).
 *
 * The highest address in the kernel virtual space are:
 *
 *  d0003fffffffffff	# with Hash MMU
 *  c00fffffffffffff	# with Radix MMU
 *
 * The top 4 bits are reserved as the region ID on hash, leaving us 8 bits
 * that can be used for the field.
 *
 * The direct IO mapping operations will then mask off those bits
 * before doing the actual access, though that only happen when
 * CONFIG_PPC_INDIRECT_MMIO is set, thus be careful when you use that
 * mechanism
 *
 * For PIO, there is a separate CONFIG_PPC_INDIRECT_PIO which makes
 * all PIO functions call through a hook.
 */

#ifdef CONFIG_PPC_INDIRECT_MMIO
#define PCI_IO_IND_TOKEN_SHIFT	52
#define PCI_IO_IND_TOKEN_MASK	(0xfful << PCI_IO_IND_TOKEN_SHIFT)
#define PCI_FIX_ADDR(addr)						\
	((PCI_IO_ADDR)(((unsigned long)(addr)) & ~PCI_IO_IND_TOKEN_MASK))
#define PCI_GET_ADDR_TOKEN(addr)					\
	(((unsigned long)(addr) & PCI_IO_IND_TOKEN_MASK) >> 		\
		PCI_IO_IND_TOKEN_SHIFT)
#define PCI_SET_ADDR_TOKEN(addr, token) 				\
do {									\
	unsigned long __a = (unsigned long)(addr);			\
	__a &= ~PCI_IO_IND_TOKEN_MASK;					\
	__a |= ((unsigned long)(token)) << PCI_IO_IND_TOKEN_SHIFT;	\
	(addr) = (void __iomem *)__a;					\
} while(0)
#else
#define PCI_FIX_ADDR(addr) (addr)
#endif


/*
 * Non ordered and non-swapping "raw" accessors
 */

static inline unsigned char __raw_readb(const volatile void __iomem *addr)
{
	return *(volatile unsigned char __force *)PCI_FIX_ADDR(addr);
}
static inline unsigned short __raw_readw(const volatile void __iomem *addr)
{
	return *(volatile unsigned short __force *)PCI_FIX_ADDR(addr);
}
static inline unsigned int __raw_readl(const volatile void __iomem *addr)
{
	return *(volatile unsigned int __force *)PCI_FIX_ADDR(addr);
}
static inline void __raw_writeb(unsigned char v, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *)PCI_FIX_ADDR(addr) = v;
}
static inline void __raw_writew(unsigned short v, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *)PCI_FIX_ADDR(addr) = v;
}
static inline void __raw_writel(unsigned int v, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *)PCI_FIX_ADDR(addr) = v;
}

#ifdef __powerpc64__
static inline unsigned long __raw_readq(const volatile void __iomem *addr)
{
	return *(volatile unsigned long __force *)PCI_FIX_ADDR(addr);
}
static inline void __raw_writeq(unsigned long v, volatile void __iomem *addr)
{
	*(volatile unsigned long __force *)PCI_FIX_ADDR(addr) = v;
}

static inline void __raw_writeq_be(unsigned long v, volatile void __iomem *addr)
{
	__raw_writeq((__force unsigned long)cpu_to_be64(v), addr);
}

/*
 * Real mode versions of the above. Those instructions are only supposed
 * to be used in hypervisor real mode as per the architecture spec.
 */
static inline void __raw_rm_writeb(u8 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__("stbcix %0,0,%1"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writew(u16 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__("sthcix %0,0,%1"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writel(u32 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__("stwcix %0,0,%1"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writeq(u64 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__("stdcix %0,0,%1"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writeq_be(u64 val, volatile void __iomem *paddr)
{
	__raw_rm_writeq((__force u64)cpu_to_be64(val), paddr);
}

static inline u8 __raw_rm_readb(volatile void __iomem *paddr)
{
	u8 ret;
	__asm__ __volatile__("lbzcix %0,0, %1"
			     : "=r" (ret) : "r" (paddr) : "memory");
	return ret;
}

static inline u16 __raw_rm_readw(volatile void __iomem *paddr)
{
	u16 ret;
	__asm__ __volatile__("lhzcix %0,0, %1"
			     : "=r" (ret) : "r" (paddr) : "memory");
	return ret;
}

static inline u32 __raw_rm_readl(volatile void __iomem *paddr)
{
	u32 ret;
	__asm__ __volatile__("lwzcix %0,0, %1"
			     : "=r" (ret) : "r" (paddr) : "memory");
	return ret;
}

static inline u64 __raw_rm_readq(volatile void __iomem *paddr)
{
	u64 ret;
	__asm__ __volatile__("ldcix %0,0, %1"
			     : "=r" (ret) : "r" (paddr) : "memory");
	return ret;
}
#endif /* __powerpc64__ */

/*
 *
 * PCI PIO and MMIO accessors.
 *
 *
 * On 32 bits, PIO operations have a recovery mechanism in case they trigger
 * machine checks (which they occasionally do when probing non existing
 * IO ports on some platforms, like PowerMac and 8xx).
 * I always found it to be of dubious reliability and I am tempted to get
 * rid of it one of these days. So if you think it's important to keep it,
 * please voice up asap. We never had it for 64 bits and I do not intend
 * to port it over
 */

#ifdef CONFIG_PPC32

#define __do_in_asm(name, op)				\
static inline unsigned int name(unsigned int port)	\
{							\
	unsigned int x;					\
	__asm__ __volatile__(				\
		"sync\n"				\
		"0:"	op "	%0,0,%1\n"		\
		"1:	twi	0,%0,0\n"		\
		"2:	isync\n"			\
		"3:	nop\n"				\
		"4:\n"					\
		".section .fixup,\"ax\"\n"		\
		"5:	li	%0,-1\n"		\
		"	b	4b\n"			\
		".previous\n"				\
		EX_TABLE(0b, 5b)			\
		EX_TABLE(1b, 5b)			\
		EX_TABLE(2b, 5b)			\
		EX_TABLE(3b, 5b)			\
		: "=&r" (x)				\
		: "r" (port + _IO_BASE)			\
		: "memory");  				\
	return x;					\
}

#define __do_out_asm(name, op)				\
static inline void name(unsigned int val, unsigned int port) \
{							\
	__asm__ __volatile__(				\
		"sync\n"				\
		"0:" op " %0,0,%1\n"			\
		"1:	sync\n"				\
		"2:\n"					\
		EX_TABLE(0b, 2b)			\
		EX_TABLE(1b, 2b)			\
		: : "r" (val), "r" (port + _IO_BASE)	\
		: "memory");   	   	   		\
}

__do_in_asm(_rec_inb, "lbzx")
__do_in_asm(_rec_inw, "lhbrx")
__do_in_asm(_rec_inl, "lwbrx")
__do_out_asm(_rec_outb, "stbx")
__do_out_asm(_rec_outw, "sthbrx")
__do_out_asm(_rec_outl, "stwbrx")

#endif /* CONFIG_PPC32 */

/* The "__do_*" operations below provide the actual "base" implementation
 * for each of the defined accessors. Some of them use the out_* functions
 * directly, some of them still use EEH, though we might change that in the
 * future. Those macros below provide the necessary argument swapping and
 * handling of the IO base for PIO.
 *
 * They are themselves used by the macros that define the actual accessors
 * and can be used by the hooks if any.
 *
 * Note that PIO operations are always defined in terms of their corresonding
 * MMIO operations. That allows platforms like iSeries who want to modify the
 * behaviour of both to only hook on the MMIO version and get both. It's also
 * possible to hook directly at the toplevel PIO operation if they have to
 * be handled differently
 */
#define __do_writeb(val, addr)	out_8(PCI_FIX_ADDR(addr), val)
#define __do_writew(val, addr)	out_le16(PCI_FIX_ADDR(addr), val)
#define __do_writel(val, addr)	out_le32(PCI_FIX_ADDR(addr), val)
#define __do_writeq(val, addr)	out_le64(PCI_FIX_ADDR(addr), val)
#define __do_writew_be(val, addr) out_be16(PCI_FIX_ADDR(addr), val)
#define __do_writel_be(val, addr) out_be32(PCI_FIX_ADDR(addr), val)
#define __do_writeq_be(val, addr) out_be64(PCI_FIX_ADDR(addr), val)

#ifdef CONFIG_EEH
#define __do_readb(addr)	eeh_readb(PCI_FIX_ADDR(addr))
#define __do_readw(addr)	eeh_readw(PCI_FIX_ADDR(addr))
#define __do_readl(addr)	eeh_readl(PCI_FIX_ADDR(addr))
#define __do_readq(addr)	eeh_readq(PCI_FIX_ADDR(addr))
#define __do_readw_be(addr)	eeh_readw_be(PCI_FIX_ADDR(addr))
#define __do_readl_be(addr)	eeh_readl_be(PCI_FIX_ADDR(addr))
#define __do_readq_be(addr)	eeh_readq_be(PCI_FIX_ADDR(addr))
#else /* CONFIG_EEH */
#define __do_readb(addr)	in_8(PCI_FIX_ADDR(addr))
#define __do_readw(addr)	in_le16(PCI_FIX_ADDR(addr))
#define __do_readl(addr)	in_le32(PCI_FIX_ADDR(addr))
#define __do_readq(addr)	in_le64(PCI_FIX_ADDR(addr))
#define __do_readw_be(addr)	in_be16(PCI_FIX_ADDR(addr))
#define __do_readl_be(addr)	in_be32(PCI_FIX_ADDR(addr))
#define __do_readq_be(addr)	in_be64(PCI_FIX_ADDR(addr))
#endif /* !defined(CONFIG_EEH) */

#ifdef CONFIG_PPC32
#define __do_outb(val, port)	_rec_outb(val, port)
#define __do_outw(val, port)	_rec_outw(val, port)
#define __do_outl(val, port)	_rec_outl(val, port)
#define __do_inb(port)		_rec_inb(port)
#define __do_inw(port)		_rec_inw(port)
#define __do_inl(port)		_rec_inl(port)
#else /* CONFIG_PPC32 */
#define __do_outb(val, port)	writeb(val,(PCI_IO_ADDR)_IO_BASE+port);
#define __do_outw(val, port)	writew(val,(PCI_IO_ADDR)_IO_BASE+port);
#define __do_outl(val, port)	writel(val,(PCI_IO_ADDR)_IO_BASE+port);
#define __do_inb(port)		readb((PCI_IO_ADDR)_IO_BASE + port);
#define __do_inw(port)		readw((PCI_IO_ADDR)_IO_BASE + port);
#define __do_inl(port)		readl((PCI_IO_ADDR)_IO_BASE + port);
#endif /* !CONFIG_PPC32 */

#ifdef CONFIG_EEH
#define __do_readsb(a, b, n)	eeh_readsb(PCI_FIX_ADDR(a), (b), (n))
#define __do_readsw(a, b, n)	eeh_readsw(PCI_FIX_ADDR(a), (b), (n))
#define __do_readsl(a, b, n)	eeh_readsl(PCI_FIX_ADDR(a), (b), (n))
#else /* CONFIG_EEH */
#define __do_readsb(a, b, n)	_insb(PCI_FIX_ADDR(a), (b), (n))
#define __do_readsw(a, b, n)	_insw(PCI_FIX_ADDR(a), (b), (n))
#define __do_readsl(a, b, n)	_insl(PCI_FIX_ADDR(a), (b), (n))
#endif /* !CONFIG_EEH */
#define __do_writesb(a, b, n)	_outsb(PCI_FIX_ADDR(a),(b),(n))
#define __do_writesw(a, b, n)	_outsw(PCI_FIX_ADDR(a),(b),(n))
#define __do_writesl(a, b, n)	_outsl(PCI_FIX_ADDR(a),(b),(n))

#define __do_insb(p, b, n)	readsb((PCI_IO_ADDR)_IO_BASE+(p), (b), (n))
#define __do_insw(p, b, n)	readsw((PCI_IO_ADDR)_IO_BASE+(p), (b), (n))
#define __do_insl(p, b, n)	readsl((PCI_IO_ADDR)_IO_BASE+(p), (b), (n))
#define __do_outsb(p, b, n)	writesb((PCI_IO_ADDR)_IO_BASE+(p),(b),(n))
#define __do_outsw(p, b, n)	writesw((PCI_IO_ADDR)_IO_BASE+(p),(b),(n))
#define __do_outsl(p, b, n)	writesl((PCI_IO_ADDR)_IO_BASE+(p),(b),(n))

#define __do_memset_io(addr, c, n)	\
				_memset_io(PCI_FIX_ADDR(addr), c, n)
#define __do_memcpy_toio(dst, src, n)	\
				_memcpy_toio(PCI_FIX_ADDR(dst), src, n)

#ifdef CONFIG_EEH
#define __do_memcpy_fromio(dst, src, n)	\
				eeh_memcpy_fromio(dst, PCI_FIX_ADDR(src), n)
#else /* CONFIG_EEH */
#define __do_memcpy_fromio(dst, src, n)	\
				_memcpy_fromio(dst,PCI_FIX_ADDR(src),n)
#endif /* !CONFIG_EEH */

#ifdef CONFIG_PPC_INDIRECT_PIO
#define DEF_PCI_HOOK_pio(x)	x
#else
#define DEF_PCI_HOOK_pio(x)	NULL
#endif

#ifdef CONFIG_PPC_INDIRECT_MMIO
#define DEF_PCI_HOOK_mem(x)	x
#else
#define DEF_PCI_HOOK_mem(x)	NULL
#endif

/* Structure containing all the hooks */
extern struct ppc_pci_io {

#define DEF_PCI_AC_RET(name, ret, at, al, space, aa)	ret (*name) at;
#define DEF_PCI_AC_NORET(name, at, al, space, aa)	void (*name) at;

#include <asm/io-defs.h>

#undef DEF_PCI_AC_RET
#undef DEF_PCI_AC_NORET

} ppc_pci_io;

/* The inline wrappers */
#define DEF_PCI_AC_RET(name, ret, at, al, space, aa)		\
static inline ret name at					\
{								\
	if (DEF_PCI_HOOK_##space(ppc_pci_io.name) != NULL)	\
		return ppc_pci_io.name al;			\
	return __do_##name al;					\
}

#define DEF_PCI_AC_NORET(name, at, al, space, aa)		\
static inline void name at					\
{								\
	if (DEF_PCI_HOOK_##space(ppc_pci_io.name) != NULL)		\
		ppc_pci_io.name al;				\
	else							\
		__do_##name al;					\
}

#include <asm/io-defs.h>

#undef DEF_PCI_AC_RET
#undef DEF_PCI_AC_NORET

/* Some drivers check for the presence of readq & writeq with
 * a #ifdef, so we make them happy here.
 */
#ifdef __powerpc64__
#define readq	readq
#define writeq	writeq
#endif

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

/*
 * We don't do relaxed operations yet, at least not with this semantic
 */
#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)
#define readq_relaxed(addr)	readq(addr)
#define writeb_relaxed(v, addr)	writeb(v, addr)
#define writew_relaxed(v, addr)	writew(v, addr)
#define writel_relaxed(v, addr)	writel(v, addr)
#define writeq_relaxed(v, addr)	writeq(v, addr)

#include <asm-generic/iomap.h>

#ifdef CONFIG_PPC32
#define mmiowb()
#else
/*
 * Enforce synchronisation of stores vs. spin_unlock
 * (this does it explicitly, though our implementation of spin_unlock
 * does it implicitely too)
 */
static inline void mmiowb(void)
{
	unsigned long tmp;

	__asm__ __volatile__("sync; li %0,0; stb %0,%1(13)"
	: "=&r" (tmp) : "i" (offsetof(struct paca_struct, io_sync))
	: "memory");
}
#endif /* !CONFIG_PPC32 */

static inline void iosync(void)
{
        __asm__ __volatile__ ("sync" : : : "memory");
}

/* Enforce in-order execution of data I/O.
 * No distinction between read/write on PPC; use eieio for all three.
 * Those are fairly week though. They don't provide a barrier between
 * MMIO and cacheable storage nor do they provide a barrier vs. locks,
 * they only provide barriers between 2 __raw MMIO operations and
 * possibly break write combining.
 */
#define iobarrier_rw() eieio()
#define iobarrier_r()  eieio()
#define iobarrier_w()  eieio()


/*
 * output pause versions need a delay at least for the
 * w83c105 ide controller in a p610.
 */
#define inb_p(port)             inb(port)
#define outb_p(val, port)       (udelay(1), outb((val), (port)))
#define inw_p(port)             inw(port)
#define outw_p(val, port)       (udelay(1), outw((val), (port)))
#define inl_p(port)             inl(port)
#define outl_p(val, port)       (udelay(1), outl((val), (port)))


#define IO_SPACE_LIMIT ~(0UL)


/**
 * ioremap     -   map bus memory into CPU space
 * @address:   bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * We provide a few variations of it:
 *
 * * ioremap is the standard one and provides non-cacheable guarded mappings
 *   and can be hooked by the platform via ppc_md
 *
 * * ioremap_prot allows to specify the page flags as an argument and can
 *   also be hooked by the platform via ppc_md.
 *
 * * ioremap_nocache is identical to ioremap
 *
 * * ioremap_wc enables write combining
 *
 * * ioremap_wt enables write through
 *
 * * ioremap_coherent maps coherent cached memory
 *
 * * iounmap undoes such a mapping and can be hooked
 *
 * * __ioremap_at (and the pending __iounmap_at) are low level functions to
 *   create hand-made mappings for use only by the PCI code and cannot
 *   currently be hooked. Must be page aligned.
 *
 * * __ioremap is the low level implementation used by ioremap and
 *   ioremap_prot and cannot be hooked (but can be used by a hook on one
 *   of the previous ones)
 *
 * * __ioremap_caller is the same as above but takes an explicit caller
 *   reference rather than using __builtin_return_address(0)
 *
 * * __iounmap, is the low level implementation used by iounmap and cannot
 *   be hooked (but can be used by a hook on iounmap)
 *
 */
extern void __iomem *ioremap(phys_addr_t address, unsigned long size);
extern void __iomem *ioremap_prot(phys_addr_t address, unsigned long size,
				  unsigned long flags);
extern void __iomem *ioremap_wc(phys_addr_t address, unsigned long size);
void __iomem *ioremap_wt(phys_addr_t address, unsigned long size);
void __iomem *ioremap_coherent(phys_addr_t address, unsigned long size);
#define ioremap_nocache(addr, size)	ioremap((addr), (size))
#define ioremap_uc(addr, size)		ioremap((addr), (size))
#define ioremap_cache(addr, size) \
	ioremap_prot((addr), (size), pgprot_val(PAGE_KERNEL))

extern void iounmap(volatile void __iomem *addr);

extern void __iomem *__ioremap(phys_addr_t, unsigned long size,
			       unsigned long flags);
extern void __iomem *__ioremap_caller(phys_addr_t, unsigned long size,
				      pgprot_t prot, void *caller);

extern void __iounmap(volatile void __iomem *addr);

extern void __iomem * __ioremap_at(phys_addr_t pa, void *ea,
				   unsigned long size, pgprot_t prot);
extern void __iounmap_at(void *ea, unsigned long size);

/*
 * When CONFIG_PPC_INDIRECT_PIO is set, we use the generic iomap implementation
 * which needs some additional definitions here. They basically allow PIO
 * space overall to be 1GB. This will work as long as we never try to use
 * iomap to map MMIO below 1GB which should be fine on ppc64
 */
#define HAVE_ARCH_PIO_SIZE		1
#define PIO_OFFSET			0x00000000UL
#define PIO_MASK			(FULL_IO_SIZE - 1)
#define PIO_RESERVED			(FULL_IO_SIZE)

#define mmio_read16be(addr)		readw_be(addr)
#define mmio_read32be(addr)		readl_be(addr)
#define mmio_read64be(addr)		readq_be(addr)
#define mmio_write16be(val, addr)	writew_be(val, addr)
#define mmio_write32be(val, addr)	writel_be(val, addr)
#define mmio_write64be(val, addr)	writeq_be(val, addr)
#define mmio_insb(addr, dst, count)	readsb(addr, dst, count)
#define mmio_insw(addr, dst, count)	readsw(addr, dst, count)
#define mmio_insl(addr, dst, count)	readsl(addr, dst, count)
#define mmio_outsb(addr, src, count)	writesb(addr, src, count)
#define mmio_outsw(addr, src, count)	writesw(addr, src, count)
#define mmio_outsl(addr, src, count)	writesl(addr, src, count)

/**
 *	virt_to_phys	-	map virtual addresses to physical
 *	@address: address to remap
 *
 *	The returned physical address is the physical (CPU) mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses directly mapped or allocated via kmalloc.
 *
 *	This function does not give bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	WARN_ON(IS_ENABLED(CONFIG_DEBUG_VIRTUAL) && !virt_addr_valid(address));

	return __pa((unsigned long)address);
}

/**
 *	phys_to_virt	-	map physical address to virtual
 *	@address: address to remap
 *
 *	The returned virtual address is a current CPU mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses that have a kernel mapping
 *
 *	This function does not handle bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */
static inline void * phys_to_virt(unsigned long address)
{
	return (void *)__va(address);
}

/*
 * Change "struct page" to physical address.
 */
static inline phys_addr_t page_to_phys(struct page *page)
{
	unsigned long pfn = page_to_pfn(page);

	WARN_ON(IS_ENABLED(CONFIG_DEBUG_VIRTUAL) && !pfn_valid(pfn));

	return PFN_PHYS(pfn);
}

/*
 * 32 bits still uses virt_to_bus() for it's implementation of DMA
 * mappings se we have to keep it defined here. We also have some old
 * drivers (shame shame shame) that use bus_to_virt() and haven't been
 * fixed yet so I need to define it here.
 */
#ifdef CONFIG_PPC32

static inline unsigned long virt_to_bus(volatile void * address)
{
        if (address == NULL)
		return 0;
        return __pa(address) + PCI_DRAM_OFFSET;
}

static inline void * bus_to_virt(unsigned long address)
{
        if (address == 0)
		return NULL;
        return __va(address - PCI_DRAM_OFFSET);
}

#define page_to_bus(page)	(page_to_phys(page) + PCI_DRAM_OFFSET)

#endif /* CONFIG_PPC32 */

/* access ports */
#define setbits32(_addr, _v) out_be32((_addr), in_be32(_addr) |  (_v))
#define clrbits32(_addr, _v) out_be32((_addr), in_be32(_addr) & ~(_v))

#define setbits16(_addr, _v) out_be16((_addr), in_be16(_addr) |  (_v))
#define clrbits16(_addr, _v) out_be16((_addr), in_be16(_addr) & ~(_v))

#define setbits8(_addr, _v) out_8((_addr), in_8(_addr) |  (_v))
#define clrbits8(_addr, _v) out_8((_addr), in_8(_addr) & ~(_v))

/* Clear and set bits in one shot.  These macros can be used to clear and
 * set multiple bits in a register using a single read-modify-write.  These
 * macros can also be used to set a multiple-bit bit pattern using a mask,
 * by specifying the mask in the 'clear' parameter and the new bit pattern
 * in the 'set' parameter.
 */

#define clrsetbits(type, addr, clear, set) \
	out_##type((addr), (in_##type(addr) & ~(clear)) | (set))

#ifdef __powerpc64__
#define clrsetbits_be64(addr, clear, set) clrsetbits(be64, addr, clear, set)
#define clrsetbits_le64(addr, clear, set) clrsetbits(le64, addr, clear, set)
#endif

#define clrsetbits_be32(addr, clear, set) clrsetbits(be32, addr, clear, set)
#define clrsetbits_le32(addr, clear, set) clrsetbits(le32, addr, clear, set)

#define clrsetbits_be16(addr, clear, set) clrsetbits(be16, addr, clear, set)
#define clrsetbits_le16(addr, clear, set) clrsetbits(le16, addr, clear, set)

#define clrsetbits_8(addr, clear, set) clrsetbits(8, addr, clear, set)

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_IO_H */
