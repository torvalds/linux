/* iommu.h: Definitions for the sun4m IOMMU.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_IOMMU_H
#define _SPARC_IOMMU_H

#include <asm/page.h>
#include <asm/bitext.h>

/* The iommu handles all virtual to physical address translations
 * that occur between the SBUS and physical memory.  Access by
 * the cpu to IO registers and similar go over the mbus so are
 * translated by the on chip SRMMU.  The iommu and the srmmu do
 * not need to have the same translations at all, in fact most
 * of the time the translations they handle are a disjunct set.
 * Basically the iommu handles all dvma sbus activity.
 */

/* The IOMMU registers occupy three pages in IO space. */
struct iommu_regs {
	/* First page */
	volatile unsigned long control;    /* IOMMU control */
	volatile unsigned long base;       /* Physical base of iopte page table */
	volatile unsigned long _unused1[3];
	volatile unsigned long tlbflush;   /* write only */
	volatile unsigned long pageflush;  /* write only */
	volatile unsigned long _unused2[1017];
	/* Second page */
	volatile unsigned long afsr;       /* Async-fault status register */
	volatile unsigned long afar;       /* Async-fault physical address */
	volatile unsigned long _unused3[2];
	volatile unsigned long sbuscfg0;   /* SBUS configuration registers, per-slot */
	volatile unsigned long sbuscfg1;
	volatile unsigned long sbuscfg2;
	volatile unsigned long sbuscfg3;
	volatile unsigned long mfsr;       /* Memory-fault status register */
	volatile unsigned long mfar;       /* Memory-fault physical address */
	volatile unsigned long _unused4[1014];
	/* Third page */
	volatile unsigned long mid;        /* IOMMU module-id */
};

#define IOMMU_CTRL_IMPL     0xf0000000 /* Implementation */
#define IOMMU_CTRL_VERS     0x0f000000 /* Version */
#define IOMMU_CTRL_RNGE     0x0000001c /* Mapping RANGE */
#define IOMMU_RNGE_16MB     0x00000000 /* 0xff000000 -> 0xffffffff */
#define IOMMU_RNGE_32MB     0x00000004 /* 0xfe000000 -> 0xffffffff */
#define IOMMU_RNGE_64MB     0x00000008 /* 0xfc000000 -> 0xffffffff */
#define IOMMU_RNGE_128MB    0x0000000c /* 0xf8000000 -> 0xffffffff */
#define IOMMU_RNGE_256MB    0x00000010 /* 0xf0000000 -> 0xffffffff */
#define IOMMU_RNGE_512MB    0x00000014 /* 0xe0000000 -> 0xffffffff */
#define IOMMU_RNGE_1GB      0x00000018 /* 0xc0000000 -> 0xffffffff */
#define IOMMU_RNGE_2GB      0x0000001c /* 0x80000000 -> 0xffffffff */
#define IOMMU_CTRL_ENAB     0x00000001 /* IOMMU Enable */

#define IOMMU_AFSR_ERR      0x80000000 /* LE, TO, or BE asserted */
#define IOMMU_AFSR_LE       0x40000000 /* SBUS reports error after transaction */
#define IOMMU_AFSR_TO       0x20000000 /* Write access took more than 12.8 us. */
#define IOMMU_AFSR_BE       0x10000000 /* Write access received error acknowledge */
#define IOMMU_AFSR_SIZE     0x0e000000 /* Size of transaction causing error */
#define IOMMU_AFSR_S        0x01000000 /* Sparc was in supervisor mode */
#define IOMMU_AFSR_RESV     0x00f00000 /* Reserver, forced to 0x8 by hardware */
#define IOMMU_AFSR_ME       0x00080000 /* Multiple errors occurred */
#define IOMMU_AFSR_RD       0x00040000 /* A read operation was in progress */
#define IOMMU_AFSR_FAV      0x00020000 /* IOMMU afar has valid contents */

#define IOMMU_SBCFG_SAB30   0x00010000 /* Phys-address bit 30 when bypass enabled */
#define IOMMU_SBCFG_BA16    0x00000004 /* Slave supports 16 byte bursts */
#define IOMMU_SBCFG_BA8     0x00000002 /* Slave supports 8 byte bursts */
#define IOMMU_SBCFG_BYPASS  0x00000001 /* Bypass IOMMU, treat all addresses
					  produced by this device as pure
					  physical. */

#define IOMMU_MFSR_ERR      0x80000000 /* One or more of PERR1 or PERR0 */
#define IOMMU_MFSR_S        0x01000000 /* Sparc was in supervisor mode */
#define IOMMU_MFSR_CPU      0x00800000 /* CPU transaction caused parity error */
#define IOMMU_MFSR_ME       0x00080000 /* Multiple parity errors occurred */
#define IOMMU_MFSR_PERR     0x00006000 /* high bit indicates parity error occurred
					  on the even word of the access, low bit
					  indicated odd word caused the parity error */
#define IOMMU_MFSR_BM       0x00001000 /* Error occurred while in boot mode */
#define IOMMU_MFSR_C        0x00000800 /* Address causing error was marked cacheable */
#define IOMMU_MFSR_RTYP     0x000000f0 /* Memory request transaction type */

#define IOMMU_MID_SBAE      0x001f0000 /* SBus arbitration enable */
#define IOMMU_MID_SE        0x00100000 /* Enables SCSI/ETHERNET arbitration */
#define IOMMU_MID_SB3       0x00080000 /* Enable SBUS device 3 arbitration */
#define IOMMU_MID_SB2       0x00040000 /* Enable SBUS device 2 arbitration */
#define IOMMU_MID_SB1       0x00020000 /* Enable SBUS device 1 arbitration */
#define IOMMU_MID_SB0       0x00010000 /* Enable SBUS device 0 arbitration */
#define IOMMU_MID_MID       0x0000000f /* Module-id, hardcoded to 0x8 */

/* The format of an iopte in the page tables */
#define IOPTE_PAGE          0x07ffff00 /* Physical page number (PA[30:12]) */
#define IOPTE_CACHE         0x00000080 /* Cached (in vme IOCACHE or Viking/MXCC) */
#define IOPTE_WRITE         0x00000004 /* Writeable */
#define IOPTE_VALID         0x00000002 /* IOPTE is valid */
#define IOPTE_WAZ           0x00000001 /* Write as zeros */

struct iommu_struct {
	struct iommu_regs *regs;
	iopte_t *page_table;
	/* For convenience */
	unsigned long start; /* First managed virtual address */
	unsigned long end;   /* Last managed virtual address */

	struct bit_map usemap;
};

static inline void iommu_invalidate(struct iommu_regs *regs)
{
	regs->tlbflush = 0;
}

static inline void iommu_invalidate_page(struct iommu_regs *regs, unsigned long ba)
{
	regs->pageflush = (ba & PAGE_MASK);
}

extern void iommu_init(struct device_node *dp, struct sbus_bus *sbus);

#endif /* !(_SPARC_IOMMU_H) */
