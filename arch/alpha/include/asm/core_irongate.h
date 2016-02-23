#ifndef __ALPHA_IRONGATE__H__
#define __ALPHA_IRONGATE__H__

#include <linux/types.h>
#include <asm/compiler.h>

/*
 * IRONGATE is the internal name for the AMD-751 K7 core logic chipset
 * which provides memory controller and PCI access for NAUTILUS-based
 * EV6 (21264) systems.
 *
 * This file is based on:
 *
 * IronGate management library, (c) 1999 Alpha Processor, Inc.
 * Copyright (C) 1999 Alpha Processor, Inc.,
 *	(David Daniel, Stig Telfer, Soohoon Lee)
 */

/*
 * The 21264 supports, and internally recognizes, a 44-bit physical
 * address space that is divided equally between memory address space
 * and I/O address space. Memory address space resides in the lower
 * half of the physical address space (PA[43]=0) and I/O address space
 * resides in the upper half of the physical address space (PA[43]=1).
 */

/*
 * Irongate CSR map.  Some of the CSRs are 8 or 16 bits, but all access
 * through the routines given is 32-bit.
 *
 * The first 0x40 bytes are standard as per the PCI spec.
 */

typedef volatile __u32	igcsr32;

typedef struct {
	igcsr32 dev_vendor;		/* 0x00 - device ID, vendor ID */
	igcsr32 stat_cmd;		/* 0x04 - status, command */
	igcsr32 class;			/* 0x08 - class code, rev ID */
	igcsr32 latency;		/* 0x0C - header type, PCI latency */
	igcsr32 bar0;			/* 0x10 - BAR0 - AGP */
	igcsr32 bar1;			/* 0x14 - BAR1 - GART */
	igcsr32 bar2;			/* 0x18 - Power Management reg block */

	igcsr32 rsrvd0[6];		/* 0x1C-0x33 reserved */

	igcsr32 capptr;			/* 0x34 - Capabilities pointer */

	igcsr32 rsrvd1[2];		/* 0x38-0x3F reserved */

	igcsr32 bacsr10;		/* 0x40 - base address chip selects */
	igcsr32 bacsr32;		/* 0x44 - base address chip selects */
	igcsr32 bacsr54_eccms761;	/* 0x48 - 751: base addr. chip selects
						  761: ECC, mode/status */

	igcsr32 rsrvd2[1];		/* 0x4C-0x4F reserved */

	igcsr32 drammap;		/* 0x50 - address mapping control */
	igcsr32 dramtm;			/* 0x54 - timing, driver strength */
	igcsr32 dramms;			/* 0x58 - DRAM mode/status */

	igcsr32 rsrvd3[1];		/* 0x5C-0x5F reserved */

	igcsr32 biu0;			/* 0x60 - bus interface unit */
	igcsr32 biusip;			/* 0x64 - Serial initialisation pkt */

	igcsr32 rsrvd4[2];		/* 0x68-0x6F reserved */

	igcsr32 mro;			/* 0x70 - memory request optimiser */

	igcsr32 rsrvd5[3];		/* 0x74-0x7F reserved */

	igcsr32 whami;			/* 0x80 - who am I */
	igcsr32 pciarb;			/* 0x84 - PCI arbitration control */
	igcsr32 pcicfg;			/* 0x88 - PCI config status */

	igcsr32 rsrvd6[4];		/* 0x8C-0x9B reserved */

	igcsr32 pci_mem;		/* 0x9C - PCI top of memory,
						  761 only */

	/* AGP (bus 1) control registers */
	igcsr32 agpcap;			/* 0xA0 - AGP Capability Identifier */
	igcsr32 agpstat;		/* 0xA4 - AGP status register */
	igcsr32 agpcmd;			/* 0xA8 - AGP control register */
	igcsr32 agpva;			/* 0xAC - AGP Virtual Address Space */
	igcsr32 agpmode;		/* 0xB0 - AGP/GART mode control */
} Irongate0;


typedef struct {

	igcsr32 dev_vendor;		/* 0x00 - Device and Vendor IDs */
	igcsr32 stat_cmd;		/* 0x04 - Status and Command regs */
	igcsr32 class;			/* 0x08 - subclass, baseclass etc */
	igcsr32 htype;			/* 0x0C - header type (at 0x0E) */
	igcsr32 rsrvd0[2];		/* 0x10-0x17 reserved */
	igcsr32 busnos;			/* 0x18 - Primary, secondary bus nos */
	igcsr32 io_baselim_regs;	/* 0x1C - IO base, IO lim, AGP status */
	igcsr32	mem_baselim;		/* 0x20 - memory base, memory lim */
	igcsr32 pfmem_baselim;		/* 0x24 - prefetchable base, lim */
	igcsr32 rsrvd1[2];		/* 0x28-0x2F reserved */
	igcsr32 io_baselim;		/* 0x30 - IO base, IO limit */
	igcsr32 rsrvd2[2];		/* 0x34-0x3B - reserved */
	igcsr32 interrupt;		/* 0x3C - interrupt, PCI bridge ctrl */

} Irongate1;

extern igcsr32 *IronECC;

/*
 * Memory spaces:
 */

/* Irongate is consistent with a subset of the Tsunami memory map */
#ifdef USE_48_BIT_KSEG
#define IRONGATE_BIAS 0x80000000000UL
#else
#define IRONGATE_BIAS 0x10000000000UL
#endif


#define IRONGATE_MEM		(IDENT_ADDR | IRONGATE_BIAS | 0x000000000UL)
#define IRONGATE_IACK_SC	(IDENT_ADDR | IRONGATE_BIAS | 0x1F8000000UL)
#define IRONGATE_IO		(IDENT_ADDR | IRONGATE_BIAS | 0x1FC000000UL)
#define IRONGATE_CONF		(IDENT_ADDR | IRONGATE_BIAS | 0x1FE000000UL)

/*
 * PCI Configuration space accesses are formed like so:
 *
 * 0x1FE << 24 |  : 2 2 2 2 1 1 1 1 : 1 1 1 1 1 1 0 0 : 0 0 0 0 0 0 0 0 :
 *                : 3 2 1 0 9 8 7 6 : 5 4 3 2 1 0 9 8 : 7 6 5 4 3 2 1 0 :
 *                  ---bus numer---   -device-- -fun-   ---register----
 */

#define IGCSR(dev,fun,reg)	( IRONGATE_CONF | \
				((dev)<<11) | \
				((fun)<<8) | \
				(reg) )

#define IRONGATE0		((Irongate0 *) IGCSR(0, 0, 0))
#define IRONGATE1		((Irongate1 *) IGCSR(1, 0, 0))

/*
 * Data structure for handling IRONGATE machine checks:
 * This is the standard OSF logout frame
 */

#define SCB_Q_SYSERR	0x620			/* OSF definitions */
#define SCB_Q_PROCERR	0x630
#define SCB_Q_SYSMCHK	0x660
#define SCB_Q_PROCMCHK	0x670

struct el_IRONGATE_sysdata_mcheck {
	__u32 FrameSize;                 /* Bytes, including this field */
	__u32 FrameFlags;                /* <31> = Retry, <30> = Second Error */
	__u32 CpuOffset;                 /* Offset to CPU-specific into */
	__u32 SystemOffset;              /* Offset to system-specific info */
	__u32 MCHK_Code;
	__u32 MCHK_Frame_Rev;
	__u64 I_STAT;
	__u64 DC_STAT;
	__u64 C_ADDR;
	__u64 DC1_SYNDROME;
	__u64 DC0_SYNDROME;
	__u64 C_STAT;
	__u64 C_STS;
	__u64 RESERVED0;
	__u64 EXC_ADDR;
	__u64 IER_CM;
	__u64 ISUM;
	__u64 MM_STAT;
	__u64 PAL_BASE;
	__u64 I_CTL;
	__u64 PCTX;
};


#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * I/O functions:
 *
 * IRONGATE (AMD-751) PCI/memory support chip for the EV6 (21264) and
 * K7 can only use linear accesses to get at PCI memory and I/O spaces.
 */

/*
 * Memory functions.  All accesses are done through linear space.
 */

__EXTERN_INLINE void __iomem *irongate_ioportmap(unsigned long addr)
{
	return (void __iomem *)(addr + IRONGATE_IO);
}

extern void __iomem *irongate_ioremap(unsigned long addr, unsigned long size);
extern void irongate_iounmap(volatile void __iomem *addr);

__EXTERN_INLINE int irongate_is_ioaddr(unsigned long addr)
{
	return addr >= IRONGATE_MEM;
}

__EXTERN_INLINE int irongate_is_mmio(const volatile void __iomem *xaddr)
{
	unsigned long addr = (unsigned long)xaddr;
	return addr < IRONGATE_IO || addr >= IRONGATE_CONF;
}

#undef __IO_PREFIX
#define __IO_PREFIX			irongate
#define irongate_trivial_rw_bw		1
#define irongate_trivial_rw_lq		1
#define irongate_trivial_io_bw		1
#define irongate_trivial_io_lq		1
#define irongate_trivial_iounmap	0
#include <asm/io_trivial.h>

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_IRONGATE__H__ */
