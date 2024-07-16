/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_LCA__H__
#define __ALPHA_LCA__H__

#include <asm/compiler.h>
#include <asm/mce.h>

/*
 * Low Cost Alpha (LCA) definitions (these apply to 21066 and 21068,
 * for example).
 *
 * This file is based on:
 *
 *	DECchip 21066 and DECchip 21068 Alpha AXP Microprocessors
 *	Hardware Reference Manual; Digital Equipment Corp.; May 1994;
 *	Maynard, MA; Order Number: EC-N2681-71.
 */

/*
 * NOTE: The LCA uses a Host Address Extension (HAE) register to access
 *	 PCI addresses that are beyond the first 27 bits of address
 *	 space.  Updating the HAE requires an external cycle (and
 *	 a memory barrier), which tends to be slow.  Instead of updating
 *	 it on each sparse memory access, we keep the current HAE value
 *	 cached in variable cache_hae.  Only if the cached HAE differs
 *	 from the desired HAE value do we actually updated HAE register.
 *	 The HAE register is preserved by the interrupt handler entry/exit
 *	 code, so this scheme works even in the presence of interrupts.
 *
 * Dense memory space doesn't require the HAE, but is restricted to
 * aligned 32 and 64 bit accesses.  Special Cycle and Interrupt
 * Acknowledge cycles may also require the use of the HAE.  The LCA
 * limits I/O address space to the bottom 24 bits of address space,
 * but this easily covers the 16 bit ISA I/O address space.
 */

/*
 * NOTE 2! The memory operations do not set any memory barriers, as
 * it's not needed for cases like a frame buffer that is essentially
 * memory-like.  You need to do them by hand if the operations depend
 * on ordering.
 *
 * Similarly, the port I/O operations do a "mb" only after a write
 * operation: if an mb is needed before (as in the case of doing
 * memory mapped I/O first, and then a port I/O operation to the same
 * device), it needs to be done by hand.
 *
 * After the above has bitten me 100 times, I'll give up and just do
 * the mb all the time, but right now I'm hoping this will work out.
 * Avoiding mb's may potentially be a noticeable speed improvement,
 * but I can't honestly say I've tested it.
 *
 * Handling interrupts that need to do mb's to synchronize to
 * non-interrupts is another fun race area.  Don't do it (because if
 * you do, I'll have to do *everything* with interrupts disabled,
 * ugh).
 */

/*
 * Memory Controller registers:
 */
#define LCA_MEM_BCR0		(IDENT_ADDR + 0x120000000UL)
#define LCA_MEM_BCR1		(IDENT_ADDR + 0x120000008UL)
#define LCA_MEM_BCR2		(IDENT_ADDR + 0x120000010UL)
#define LCA_MEM_BCR3		(IDENT_ADDR + 0x120000018UL)
#define LCA_MEM_BMR0		(IDENT_ADDR + 0x120000020UL)
#define LCA_MEM_BMR1		(IDENT_ADDR + 0x120000028UL)
#define LCA_MEM_BMR2		(IDENT_ADDR + 0x120000030UL)
#define LCA_MEM_BMR3		(IDENT_ADDR + 0x120000038UL)
#define LCA_MEM_BTR0		(IDENT_ADDR + 0x120000040UL)
#define LCA_MEM_BTR1		(IDENT_ADDR + 0x120000048UL)
#define LCA_MEM_BTR2		(IDENT_ADDR + 0x120000050UL)
#define LCA_MEM_BTR3		(IDENT_ADDR + 0x120000058UL)
#define LCA_MEM_GTR		(IDENT_ADDR + 0x120000060UL)
#define LCA_MEM_ESR		(IDENT_ADDR + 0x120000068UL)
#define LCA_MEM_EAR		(IDENT_ADDR + 0x120000070UL)
#define LCA_MEM_CAR		(IDENT_ADDR + 0x120000078UL)
#define LCA_MEM_VGR		(IDENT_ADDR + 0x120000080UL)
#define LCA_MEM_PLM		(IDENT_ADDR + 0x120000088UL)
#define LCA_MEM_FOR		(IDENT_ADDR + 0x120000090UL)

/*
 * I/O Controller registers:
 */
#define LCA_IOC_HAE		(IDENT_ADDR + 0x180000000UL)
#define LCA_IOC_CONF		(IDENT_ADDR + 0x180000020UL)
#define LCA_IOC_STAT0		(IDENT_ADDR + 0x180000040UL)
#define LCA_IOC_STAT1		(IDENT_ADDR + 0x180000060UL)
#define LCA_IOC_TBIA		(IDENT_ADDR + 0x180000080UL)
#define LCA_IOC_TB_ENA		(IDENT_ADDR + 0x1800000a0UL)
#define LCA_IOC_SFT_RST		(IDENT_ADDR + 0x1800000c0UL)
#define LCA_IOC_PAR_DIS		(IDENT_ADDR + 0x1800000e0UL)
#define LCA_IOC_W_BASE0		(IDENT_ADDR + 0x180000100UL)
#define LCA_IOC_W_BASE1		(IDENT_ADDR + 0x180000120UL)
#define LCA_IOC_W_MASK0		(IDENT_ADDR + 0x180000140UL)
#define LCA_IOC_W_MASK1		(IDENT_ADDR + 0x180000160UL)
#define LCA_IOC_T_BASE0		(IDENT_ADDR + 0x180000180UL)
#define LCA_IOC_T_BASE1		(IDENT_ADDR + 0x1800001a0UL)
#define LCA_IOC_TB_TAG0		(IDENT_ADDR + 0x188000000UL)
#define LCA_IOC_TB_TAG1		(IDENT_ADDR + 0x188000020UL)
#define LCA_IOC_TB_TAG2		(IDENT_ADDR + 0x188000040UL)
#define LCA_IOC_TB_TAG3		(IDENT_ADDR + 0x188000060UL)
#define LCA_IOC_TB_TAG4		(IDENT_ADDR + 0x188000070UL)
#define LCA_IOC_TB_TAG5		(IDENT_ADDR + 0x1880000a0UL)
#define LCA_IOC_TB_TAG6		(IDENT_ADDR + 0x1880000c0UL)
#define LCA_IOC_TB_TAG7		(IDENT_ADDR + 0x1880000e0UL)

/*
 * Memory spaces:
 */
#define LCA_IACK_SC		(IDENT_ADDR + 0x1a0000000UL)
#define LCA_CONF		(IDENT_ADDR + 0x1e0000000UL)
#define LCA_IO			(IDENT_ADDR + 0x1c0000000UL)
#define LCA_SPARSE_MEM		(IDENT_ADDR + 0x200000000UL)
#define LCA_DENSE_MEM		(IDENT_ADDR + 0x300000000UL)

/*
 * Bit definitions for I/O Controller status register 0:
 */
#define LCA_IOC_STAT0_CMD		0xf
#define LCA_IOC_STAT0_ERR		(1<<4)
#define LCA_IOC_STAT0_LOST		(1<<5)
#define LCA_IOC_STAT0_THIT		(1<<6)
#define LCA_IOC_STAT0_TREF		(1<<7)
#define LCA_IOC_STAT0_CODE_SHIFT	8
#define LCA_IOC_STAT0_CODE_MASK		0x7
#define LCA_IOC_STAT0_P_NBR_SHIFT	13
#define LCA_IOC_STAT0_P_NBR_MASK	0x7ffff

#define LCA_HAE_ADDRESS		LCA_IOC_HAE

/* LCA PMR Power Management register defines */
#define LCA_PMR_ADDR	(IDENT_ADDR + 0x120000098UL)
#define LCA_PMR_PDIV    0x7                     /* Primary clock divisor */
#define LCA_PMR_ODIV    0x38                    /* Override clock divisor */
#define LCA_PMR_INTO    0x40                    /* Interrupt override */
#define LCA_PMR_DMAO    0x80                    /* DMA override */
#define LCA_PMR_OCCEB   0xffff0000L             /* Override cycle counter - even bits */
#define LCA_PMR_OCCOB   0xffff000000000000L     /* Override cycle counter - even bits */
#define LCA_PMR_PRIMARY_MASK    0xfffffffffffffff8L

/* LCA PMR Macros */

#define LCA_READ_PMR        (*(volatile unsigned long *)LCA_PMR_ADDR)
#define LCA_WRITE_PMR(d)    (*((volatile unsigned long *)LCA_PMR_ADDR) = (d))

#define LCA_GET_PRIMARY(r)  ((r) & LCA_PMR_PDIV)
#define LCA_GET_OVERRIDE(r) (((r) >> 3) & LCA_PMR_PDIV)
#define LCA_SET_PRIMARY_CLOCK(r, c) ((r) = (((r) & LCA_PMR_PRIMARY_MASK)|(c)))

/* LCA PMR Divisor values */
#define LCA_PMR_DIV_1   0x0
#define LCA_PMR_DIV_1_5 0x1
#define LCA_PMR_DIV_2   0x2
#define LCA_PMR_DIV_4   0x3
#define LCA_PMR_DIV_8   0x4
#define LCA_PMR_DIV_16  0x5
#define LCA_PMR_DIV_MIN DIV_1
#define LCA_PMR_DIV_MAX DIV_16


/*
 * Data structure for handling LCA machine checks.  Correctable errors
 * result in a short logout frame, uncorrectable ones in a long one.
 */
struct el_lca_mcheck_short {
	struct el_common	h;		/* common logout header */
	unsigned long		esr;		/* error-status register */
	unsigned long		ear;		/* error-address register */
	unsigned long		dc_stat;	/* dcache status register */
	unsigned long		ioc_stat0;	/* I/O controller status register 0 */
	unsigned long		ioc_stat1;	/* I/O controller status register 1 */
};

struct el_lca_mcheck_long {
	struct el_common	h;		/* common logout header */
	unsigned long		pt[31];		/* PAL temps */
	unsigned long		exc_addr;	/* exception address */
	unsigned long		pad1[3];
	unsigned long		pal_base;	/* PALcode base address */
	unsigned long		hier;		/* hw interrupt enable */
	unsigned long		hirr;		/* hw interrupt request */
	unsigned long		mm_csr;		/* MMU control & status */
	unsigned long		dc_stat;	/* data cache status */
	unsigned long		dc_addr;	/* data cache addr register */
	unsigned long		abox_ctl;	/* address box control register */
	unsigned long		esr;		/* error status register */
	unsigned long		ear;		/* error address register */
	unsigned long		car;		/* cache control register */
	unsigned long		ioc_stat0;	/* I/O controller status register 0 */
	unsigned long		ioc_stat1;	/* I/O controller status register 1 */
	unsigned long		va;		/* virtual address register */
};

union el_lca {
	struct el_common *		c;
	struct el_lca_mcheck_long *	l;
	struct el_lca_mcheck_short *	s;
};

#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * I/O functions:
 *
 * Unlike Jensen, the Noname machines have no concept of local
 * I/O---everything goes over the PCI bus.
 *
 * There is plenty room for optimization here.  In particular,
 * the Alpha's insb/insw/extb/extw should be useful in moving
 * data to/from the right byte-lanes.
 */

#define vip	volatile int __force *
#define vuip	volatile unsigned int __force *
#define vulp	volatile unsigned long __force *

#define LCA_SET_HAE						\
	do {							\
		if (addr >= (1UL << 24)) {			\
			unsigned long msb = addr & 0xf8000000;	\
			addr -= msb;				\
			set_hae(msb);				\
		}						\
	} while (0)


__EXTERN_INLINE u8 lca_ioread8(const void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	unsigned long result, base_and_type;

	if (addr >= LCA_DENSE_MEM) {
		addr -= LCA_DENSE_MEM;
		LCA_SET_HAE;
		base_and_type = LCA_SPARSE_MEM + 0x00;
	} else {
		addr -= LCA_IO;
		base_and_type = LCA_IO + 0x00;
	}

	result = *(vip) ((addr << 5) + base_and_type);
	return __kernel_extbl(result, addr & 3);
}

__EXTERN_INLINE void lca_iowrite8(u8 b, void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	unsigned long w, base_and_type;

	if (addr >= LCA_DENSE_MEM) {
		addr -= LCA_DENSE_MEM;
		LCA_SET_HAE;
		base_and_type = LCA_SPARSE_MEM + 0x00;
	} else {
		addr -= LCA_IO;
		base_and_type = LCA_IO + 0x00;
	}

	w = __kernel_insbl(b, addr & 3);
	*(vuip) ((addr << 5) + base_and_type) = w;
}

__EXTERN_INLINE u16 lca_ioread16(const void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	unsigned long result, base_and_type;

	if (addr >= LCA_DENSE_MEM) {
		addr -= LCA_DENSE_MEM;
		LCA_SET_HAE;
		base_and_type = LCA_SPARSE_MEM + 0x08;
	} else {
		addr -= LCA_IO;
		base_and_type = LCA_IO + 0x08;
	}

	result = *(vip) ((addr << 5) + base_and_type);
	return __kernel_extwl(result, addr & 3);
}

__EXTERN_INLINE void lca_iowrite16(u16 b, void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	unsigned long w, base_and_type;

	if (addr >= LCA_DENSE_MEM) {
		addr -= LCA_DENSE_MEM;
		LCA_SET_HAE;
		base_and_type = LCA_SPARSE_MEM + 0x08;
	} else {
		addr -= LCA_IO;
		base_and_type = LCA_IO + 0x08;
	}

	w = __kernel_inswl(b, addr & 3);
	*(vuip) ((addr << 5) + base_and_type) = w;
}

__EXTERN_INLINE u32 lca_ioread32(const void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (addr < LCA_DENSE_MEM)
		addr = ((addr - LCA_IO) << 5) + LCA_IO + 0x18;
	return *(vuip)addr;
}

__EXTERN_INLINE void lca_iowrite32(u32 b, void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (addr < LCA_DENSE_MEM)
		addr = ((addr - LCA_IO) << 5) + LCA_IO + 0x18;
	*(vuip)addr = b;
}

__EXTERN_INLINE u64 lca_ioread64(const void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (addr < LCA_DENSE_MEM)
		addr = ((addr - LCA_IO) << 5) + LCA_IO + 0x18;
	return *(vulp)addr;
}

__EXTERN_INLINE void lca_iowrite64(u64 b, void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (addr < LCA_DENSE_MEM)
		addr = ((addr - LCA_IO) << 5) + LCA_IO + 0x18;
	*(vulp)addr = b;
}

__EXTERN_INLINE void __iomem *lca_ioportmap(unsigned long addr)
{
	return (void __iomem *)(addr + LCA_IO);
}

__EXTERN_INLINE void __iomem *lca_ioremap(unsigned long addr,
					  unsigned long size)
{
	return (void __iomem *)(addr + LCA_DENSE_MEM);
}

__EXTERN_INLINE int lca_is_ioaddr(unsigned long addr)
{
	return addr >= IDENT_ADDR + 0x120000000UL;
}

__EXTERN_INLINE int lca_is_mmio(const volatile void __iomem *addr)
{
	return (unsigned long)addr >= LCA_DENSE_MEM;
}

#undef vip
#undef vuip
#undef vulp

#undef __IO_PREFIX
#define __IO_PREFIX		lca
#define lca_trivial_rw_bw	2
#define lca_trivial_rw_lq	1
#define lca_trivial_io_bw	0
#define lca_trivial_io_lq	0
#define lca_trivial_iounmap	1
#include <asm/io_trivial.h>

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_LCA__H__ */
