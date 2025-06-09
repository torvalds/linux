/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_IO_H
#define _ASM_POWERPC_IO_H
#ifdef __KERNEL__

/*
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
#include <asm/mmiowb.h>
#include <asm/mmu.h>

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

/* 32 bits uses slightly different variables for the various IO
 * bases. Most of this file only uses _IO_BASE though which we
 * define properly based on the platform
 */
#ifndef CONFIG_PCI
#define _IO_BASE	POISON_POINTER_DELTA
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
#ifdef CONFIG_PPC_INDIRECT_PIO
#error CONFIG_PPC_INDIRECT_PIO is not yet supported on 32 bits
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
 *	_insb, _insw, _insl, _outsb, _outsw, _outsl
 *
 * Those operate directly on a kernel virtual address. Note that the prototype
 * for the out_* accessors has the arguments in opposite order from the usual
 * linux PCI accessors. Unlike those, they take the address first and the value
 * next.
 */

/* -mprefixed can generate offsets beyond range, fall back hack */
#ifdef CONFIG_PPC_KERNEL_PREFIXED
#define DEF_MMIO_IN_X(name, size, insn)				\
static inline u##size name(const volatile u##size __iomem *addr)	\
{									\
	u##size ret;							\
	__asm__ __volatile__("sync;"#insn" %0,0,%1;twi 0,%0,0;isync"	\
		: "=r" (ret) : "r" (addr) : "memory");			\
	return ret;							\
}

#define DEF_MMIO_OUT_X(name, size, insn)				\
static inline void name(volatile u##size __iomem *addr, u##size val)	\
{									\
	__asm__ __volatile__("sync;"#insn" %1,0,%0"			\
		: : "r" (addr), "r" (val) : "memory");			\
	mmiowb_set_pending();						\
}

#define DEF_MMIO_IN_D(name, size, insn)				\
static inline u##size name(const volatile u##size __iomem *addr)	\
{									\
	u##size ret;							\
	__asm__ __volatile__("sync;"#insn" %0,0(%1);twi 0,%0,0;isync"\
		: "=r" (ret) : "b" (addr) : "memory");	\
	return ret;							\
}

#define DEF_MMIO_OUT_D(name, size, insn)				\
static inline void name(volatile u##size __iomem *addr, u##size val)	\
{									\
	__asm__ __volatile__("sync;"#insn" %1,0(%0)"			\
		: : "b" (addr), "r" (val) : "memory");	\
	mmiowb_set_pending();						\
}
#else
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
	mmiowb_set_pending();						\
}

#define DEF_MMIO_IN_D(name, size, insn)				\
static inline u##size name(const volatile u##size __iomem *addr)	\
{									\
	u##size ret;							\
	__asm__ __volatile__("sync;"#insn"%U1%X1 %0,%1;twi 0,%0,0;isync"\
		: "=r" (ret) : "m<>" (*addr) : "memory");	\
	return ret;							\
}

#define DEF_MMIO_OUT_D(name, size, insn)				\
static inline void name(volatile u##size __iomem *addr, u##size val)	\
{									\
	__asm__ __volatile__("sync;"#insn"%U0%X0 %1,%0"			\
		: "=m<>" (*addr) : "r" (val) : "memory");	\
	mmiowb_set_pending();						\
}
#endif

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
extern void _insw(const volatile u16 __iomem *addr, void *buf, long count);
extern void _outsw(volatile u16 __iomem *addr, const void *buf, long count);
extern void _insl(const volatile u32 __iomem *addr, void *buf, long count);
extern void _outsl(volatile u32 __iomem *addr, const void *buf, long count);

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
 * In addition, they support a hook mechanism when CONFIG_PPC_INDIRECT_PIO
 * is set allowing the platform to provide its own implementation of some
 * of the accessors.
 */

/*
 * Include the EEH definitions when EEH is enabled only so they don't get
 * in the way when building for 32 bits
 */
#ifdef CONFIG_EEH
#include <asm/eeh.h>
#endif

#define _IO_PORT(port)	((volatile void __iomem *)(_IO_BASE + (port)))

#ifdef __powerpc64__
/*
 * Real mode versions of raw accessors. Those instructions are only supposed
 * to be used in hypervisor real mode as per the architecture spec.
 */
static inline void __raw_rm_writeb(u8 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      stbcix %0,0,%1;  \
			      .machine pop;"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writew(u16 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      sthcix %0,0,%1;  \
			      .machine pop;"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writel(u32 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      stwcix %0,0,%1;  \
			      .machine pop;"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writeq(u64 val, volatile void __iomem *paddr)
{
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      stdcix %0,0,%1;  \
			      .machine pop;"
		: : "r" (val), "r" (paddr) : "memory");
}

static inline void __raw_rm_writeq_be(u64 val, volatile void __iomem *paddr)
{
	__raw_rm_writeq((__force u64)cpu_to_be64(val), paddr);
}

static inline u8 __raw_rm_readb(volatile void __iomem *paddr)
{
	u8 ret;
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      lbzcix %0,0, %1; \
			      .machine pop;"
			     : "=r" (ret) : "r" (paddr) : "memory");
	return ret;
}

static inline u16 __raw_rm_readw(volatile void __iomem *paddr)
{
	u16 ret;
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      lhzcix %0,0, %1; \
			      .machine pop;"
			     : "=r" (ret) : "r" (paddr) : "memory");
	return ret;
}

static inline u32 __raw_rm_readl(volatile void __iomem *paddr)
{
	u32 ret;
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      lwzcix %0,0, %1; \
			      .machine pop;"
			     : "=r" (ret) : "r" (paddr) : "memory");
	return ret;
}

static inline u64 __raw_rm_readq(volatile void __iomem *paddr)
{
	u64 ret;
	__asm__ __volatile__(".machine push;   \
			      .machine power6; \
			      ldcix %0,0, %1;  \
			      .machine pop;"
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

#ifdef CONFIG_EEH
#define __do_readb(addr)	eeh_readb(addr)
#define __do_readw(addr)	eeh_readw(addr)
#define __do_readl(addr)	eeh_readl(addr)
#define __do_readq(addr)	eeh_readq(addr)
#define __do_readw_be(addr)	eeh_readw_be(addr)
#define __do_readl_be(addr)	eeh_readl_be(addr)
#define __do_readq_be(addr)	eeh_readq_be(addr)
#else /* CONFIG_EEH */
#define __do_readb(addr)	in_8(addr)
#define __do_readw(addr)	in_le16(addr)
#define __do_readl(addr)	in_le32(addr)
#define __do_readq(addr)	in_le64(addr)
#define __do_readw_be(addr)	in_be16(addr)
#define __do_readl_be(addr)	in_be32(addr)
#define __do_readq_be(addr)	in_be64(addr)
#endif /* !defined(CONFIG_EEH) */

#ifdef CONFIG_PPC32
#define __do_outb(val, port)	_rec_outb(val, port)
#define __do_outw(val, port)	_rec_outw(val, port)
#define __do_outl(val, port)	_rec_outl(val, port)
#define __do_inb(port)		_rec_inb(port)
#define __do_inw(port)		_rec_inw(port)
#define __do_inl(port)		_rec_inl(port)
#else /* CONFIG_PPC32 */
#define __do_outb(val, port)	writeb(val,_IO_PORT(port));
#define __do_outw(val, port)	writew(val,_IO_PORT(port));
#define __do_outl(val, port)	writel(val,_IO_PORT(port));
#define __do_inb(port)		readb(_IO_PORT(port));
#define __do_inw(port)		readw(_IO_PORT(port));
#define __do_inl(port)		readl(_IO_PORT(port));
#endif /* !CONFIG_PPC32 */

#ifdef CONFIG_EEH
#define __do_readsb(a, b, n)	eeh_readsb(a, (b), (n))
#define __do_readsw(a, b, n)	eeh_readsw(a, (b), (n))
#define __do_readsl(a, b, n)	eeh_readsl(a, (b), (n))
#else /* CONFIG_EEH */
#define __do_readsb(a, b, n)	_insb(a, (b), (n))
#define __do_readsw(a, b, n)	_insw(a, (b), (n))
#define __do_readsl(a, b, n)	_insl(a, (b), (n))
#endif /* !CONFIG_EEH */
#define __do_writesb(a, b, n)	_outsb(a, (b), (n))
#define __do_writesw(a, b, n)	_outsw(a, (b), (n))
#define __do_writesl(a, b, n)	_outsl(a, (b), (n))

#define __do_insb(p, b, n)	readsb(_IO_PORT(p), (b), (n))
#define __do_insw(p, b, n)	readsw(_IO_PORT(p), (b), (n))
#define __do_insl(p, b, n)	readsl(_IO_PORT(p), (b), (n))
#define __do_outsb(p, b, n)	writesb(_IO_PORT(p),(b),(n))
#define __do_outsw(p, b, n)	writesw(_IO_PORT(p),(b),(n))
#define __do_outsl(p, b, n)	writesl(_IO_PORT(p),(b),(n))

#ifdef CONFIG_EEH
#define __do_memcpy_fromio(dst, src, n)	\
				eeh_memcpy_fromio(dst, src, n)
#else /* CONFIG_EEH */
#define __do_memcpy_fromio(dst, src, n)	\
				_memcpy_fromio(dst, src, n)
#endif /* !CONFIG_EEH */

static inline u8 readb(const volatile void __iomem *addr)
{
	return __do_readb(addr);
}
#define readb readb

static inline u16 readw(const volatile void __iomem *addr)
{
	return __do_readw(addr);
}
#define readw readw

static inline u32 readl(const volatile void __iomem *addr)
{
	return __do_readl(addr);
}
#define readl readl

static inline u16 readw_be(const volatile void __iomem *addr)
{
	return __do_readw_be(addr);
}

static inline u32 readl_be(const volatile void __iomem *addr)
{
	return __do_readl_be(addr);
}

static inline void writeb(u8 val, volatile void __iomem *addr)
{
	out_8(addr, val);
}
#define writeb writeb

static inline void writew(u16 val, volatile void __iomem *addr)
{
	out_le16(addr, val);
}
#define writew writew

static inline void writel(u32 val, volatile void __iomem *addr)
{
	out_le32(addr, val);
}
#define writel writel

static inline void writew_be(u16 val, volatile void __iomem *addr)
{
	out_be16(addr, val);
}

static inline void writel_be(u32 val, volatile void __iomem *addr)
{
	out_be32(addr, val);
}

static inline void readsb(const volatile void __iomem *a, void *b, unsigned long c)
{
	__do_readsb(a, b, c);
}
#define readsb readsb

static inline void readsw(const volatile void __iomem *a, void *b, unsigned long c)
{
	__do_readsw(a, b, c);
}
#define readsw readsw

static inline void readsl(const volatile void __iomem *a, void *b, unsigned long c)
{
	__do_readsl(a, b, c);
}
#define readsl readsl

static inline void writesb(volatile void __iomem *a, const void *b, unsigned long c)
{
	__do_writesb(a, b, c);
}
#define writesb writesb

static inline void writesw(volatile void __iomem *a, const void *b, unsigned long c)
{
	__do_writesw(a, b, c);
}
#define writesw writesw

static inline void writesl(volatile void __iomem *a, const void *b, unsigned long c)
{
	__do_writesl(a, b, c);
}
#define writesl writesl

static inline void memset_io(volatile void __iomem *a, int c, unsigned long n)
{
	_memset_io(a, c, n);
}
#define memset_io memset_io

static inline void memcpy_fromio(void *d, const volatile void __iomem *s, unsigned long n)
{
	__do_memcpy_fromio(d, s, n);
}
#define memcpy_fromio memcpy_fromio

static inline void memcpy_toio(volatile void __iomem *d, const void *s, unsigned long n)
{
	_memcpy_toio(d, s, n);
}
#define memcpy_toio memcpy_toio

#ifdef __powerpc64__
static inline u64 readq(const volatile void __iomem *addr)
{
	return __do_readq(addr);
}

static inline u64 readq_be(const volatile void __iomem *addr)
{
	return __do_readq_be(addr);
}

static inline void writeq(u64 val, volatile void __iomem *addr)
{
	out_le64(addr, val);
}

static inline void writeq_be(u64 val, volatile void __iomem *addr)
{
	out_be64(addr, val);
}
#endif /* __powerpc64__ */

#ifdef CONFIG_PPC_INDIRECT_PIO
#define DEF_PCI_HOOK(x)	x
#else
#define DEF_PCI_HOOK(x)	NULL
#endif

/* Structure containing all the hooks */
extern struct ppc_pci_io {

#define DEF_PCI_AC_RET(name, ret, at, al)	ret (*name) at;
#define DEF_PCI_AC_NORET(name, at, al)		void (*name) at;

#include <asm/io-defs.h>

#undef DEF_PCI_AC_RET
#undef DEF_PCI_AC_NORET

} ppc_pci_io;

/* The inline wrappers */
#define DEF_PCI_AC_RET(name, ret, at, al)			\
static inline ret name at					\
{								\
	if (DEF_PCI_HOOK(ppc_pci_io.name) != NULL)		\
		return ppc_pci_io.name al;			\
	return __do_##name al;					\
}

#define DEF_PCI_AC_NORET(name, at, al)		\
static inline void name at					\
{								\
	if (DEF_PCI_HOOK(ppc_pci_io.name) != NULL)		\
		ppc_pci_io.name al;				\
	else							\
		__do_##name al;					\
}

#include <asm/io-defs.h>

#undef DEF_PCI_AC_RET
#undef DEF_PCI_AC_NORET

// Signal to asm-generic/io.h that we have implemented these.
#define inb inb
#define inw inw
#define inl inl
#define outb outb
#define outw outw
#define outl outl
#define insb insb
#define insw insw
#define insl insl
#define outsb outsb
#define outsw outsw
#define outsl outsl
#ifdef __powerpc64__
#define readq	readq
#define writeq	writeq
#endif

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

#ifndef CONFIG_GENERIC_IOMAP
/*
 * Here comes the implementation of the IOMAP interfaces.
 */
static inline unsigned int ioread16be(const void __iomem *addr)
{
	return readw_be(addr);
}
#define ioread16be ioread16be

static inline unsigned int ioread32be(const void __iomem *addr)
{
	return readl_be(addr);
}
#define ioread32be ioread32be

#ifdef __powerpc64__
static inline u64 ioread64be(const void __iomem *addr)
{
	return readq_be(addr);
}
#define ioread64be ioread64be
#endif /* __powerpc64__ */

static inline void iowrite16be(u16 val, void __iomem *addr)
{
	writew_be(val, addr);
}
#define iowrite16be iowrite16be

static inline void iowrite32be(u32 val, void __iomem *addr)
{
	writel_be(val, addr);
}
#define iowrite32be iowrite32be

#ifdef __powerpc64__
static inline void iowrite64be(u64 val, void __iomem *addr)
{
	writeq_be(val, addr);
}
#define iowrite64be iowrite64be
#endif /* __powerpc64__ */

struct pci_dev;
void pci_iounmap(struct pci_dev *dev, void __iomem *addr);
#define pci_iounmap pci_iounmap
void __iomem *ioport_map(unsigned long port, unsigned int len);
#define ioport_map ioport_map
#endif

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
 * * ioremap_wc enables write combining
 *
 * * ioremap_wt enables write through
 *
 * * ioremap_coherent maps coherent cached memory
 *
 * * iounmap undoes such a mapping and can be hooked
 *
 * * __ioremap_caller is the same as above but takes an explicit caller
 *   reference rather than using __builtin_return_address(0)
 *
 */
extern void __iomem *ioremap(phys_addr_t address, unsigned long size);
#define ioremap ioremap
#define ioremap_prot ioremap_prot
extern void __iomem *ioremap_wc(phys_addr_t address, unsigned long size);
#define ioremap_wc ioremap_wc

#ifdef CONFIG_PPC32
void __iomem *ioremap_wt(phys_addr_t address, unsigned long size);
#define ioremap_wt ioremap_wt
#endif

void __iomem *ioremap_coherent(phys_addr_t address, unsigned long size);
#define ioremap_cache(addr, size) \
	ioremap_prot((addr), (size), PAGE_KERNEL)

#define iounmap iounmap

void __iomem *ioremap_phb(phys_addr_t paddr, unsigned long size);

int early_ioremap_range(unsigned long ea, phys_addr_t pa,
			unsigned long size, pgprot_t prot);

extern void __iomem *__ioremap_caller(phys_addr_t, unsigned long size,
				      pgprot_t prot, void *caller);

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
static inline unsigned long virt_to_phys(const volatile void * address)
{
	WARN_ON(IS_ENABLED(CONFIG_DEBUG_VIRTUAL) && !virt_addr_valid(address));

	return __pa((unsigned long)address);
}
#define virt_to_phys virt_to_phys

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
#define phys_to_virt phys_to_virt

/*
 * 32 bits still uses virt_to_bus() for its implementation of DMA
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
#define virt_to_bus virt_to_bus

static inline void * bus_to_virt(unsigned long address)
{
        if (address == 0)
		return NULL;
        return __va(address - PCI_DRAM_OFFSET);
}
#define bus_to_virt bus_to_virt

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

#include <asm-generic/io.h>

#ifdef __powerpc64__
static inline void __raw_writeq_be(unsigned long v, volatile void __iomem *addr)
{
	__raw_writeq((__force unsigned long)cpu_to_be64(v), addr);
}
#define __raw_writeq_be __raw_writeq_be
#endif // __powerpc64__

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_IO_H */
