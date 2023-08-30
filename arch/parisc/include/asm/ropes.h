/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_ROPES_H_
#define _ASM_PARISC_ROPES_H_

#include <asm/parisc-device.h>

#ifdef CONFIG_64BIT
/* "low end" PA8800 machines use ZX1 chipset: PAT PDC and only run 64-bit */
#define ZX1_SUPPORT
#endif

#ifdef CONFIG_PROC_FS
/* depends on proc fs support. But costs CPU performance */
#undef SBA_COLLECT_STATS
#endif

/*
** The number of pdir entries to "free" before issuing
** a read to PCOM register to flush out PCOM writes.
** Interacts with allocation granularity (ie 4 or 8 entries
** allocated and free'd/purged at a time might make this
** less interesting).
*/
#define DELAYED_RESOURCE_CNT	16

#define MAX_IOC		2	/* per Ike. Pluto/Astro only have 1. */
#define ROPES_PER_IOC	8	/* per Ike half or Pluto/Astro */

struct ioc {
	void __iomem	*ioc_hpa;	/* I/O MMU base address */
	char		*res_map;	/* resource map, bit == pdir entry */
	u64		*pdir_base;	/* physical base address */
	unsigned long	ibase;		/* pdir IOV Space base - shared w/lba_pci */
	unsigned long	imask;		/* pdir IOV Space mask - shared w/lba_pci */
#ifdef ZX1_SUPPORT
	unsigned long	iovp_mask;	/* help convert IOVA to IOVP */
#endif
	unsigned long	*res_hint;	/* next avail IOVP - circular search */
	spinlock_t	res_lock;
	unsigned int	res_bitshift;	/* from the LEFT! */
	unsigned int	res_size;	/* size of resource map in bytes */
#ifdef SBA_HINT_SUPPORT
/* FIXME : DMA HINTs not used */
	unsigned long	hint_mask_pdir; /* bits used for DMA hints */
	unsigned int	hint_shift_pdir;
#endif
#if DELAYED_RESOURCE_CNT > 0
	int		saved_cnt;
	struct sba_dma_pair {
			dma_addr_t	iova;
			size_t		size;
        } saved[DELAYED_RESOURCE_CNT];
#endif

#ifdef SBA_COLLECT_STATS
#define SBA_SEARCH_SAMPLE	0x100
	unsigned long	avg_search[SBA_SEARCH_SAMPLE];
	unsigned long	avg_idx;	/* current index into avg_search */
	unsigned long	used_pages;
	unsigned long	msingle_calls;
	unsigned long	msingle_pages;
	unsigned long	msg_calls;
	unsigned long	msg_pages;
	unsigned long	usingle_calls;
	unsigned long	usingle_pages;
	unsigned long	usg_calls;
	unsigned long	usg_pages;
#endif
        /* STUFF We don't need in performance path */
	unsigned int	pdir_size;	/* in bytes, determined by IOV Space size */
};

struct sba_device {
	struct sba_device	*next;  /* list of SBA's in system */
	struct parisc_device	*dev;   /* dev found in bus walk */
	const char		*name;
	void __iomem		*sba_hpa; /* base address */
	spinlock_t		sba_lock;
	unsigned int		flags;  /* state/functionality enabled */
	unsigned int		hw_rev;  /* HW revision of chip */

	struct resource		chip_resv; /* MMIO reserved for chip */
	struct resource		iommu_resv; /* MMIO reserved for iommu */

	unsigned int		num_ioc;  /* number of on-board IOC's */
	struct ioc		ioc[MAX_IOC];
};

/* list of SBA's in system, see drivers/parisc/sba_iommu.c */
extern struct sba_device *sba_list;

#define ASTRO_RUNWAY_PORT	0x582
#define IKE_MERCED_PORT		0x803
#define REO_MERCED_PORT		0x804
#define REOG_MERCED_PORT	0x805
#define PLUTO_MCKINLEY_PORT	0x880

static inline int IS_ASTRO(struct parisc_device *d) {
	return d->id.hversion == ASTRO_RUNWAY_PORT;
}

static inline int IS_IKE(struct parisc_device *d) {
	return d->id.hversion == IKE_MERCED_PORT;
}

static inline int IS_PLUTO(struct parisc_device *d) {
	return d->id.hversion == PLUTO_MCKINLEY_PORT;
}

#define PLUTO_IOVA_BASE	(1UL*1024*1024*1024)	/* 1GB */
#define PLUTO_IOVA_SIZE	(1UL*1024*1024*1024)	/* 1GB */
#define PLUTO_GART_SIZE	(PLUTO_IOVA_SIZE / 2)

#define SBA_PDIR_VALID_BIT	0x8000000000000000ULL

#define SBA_AGPGART_COOKIE	0x0000badbadc0ffeeULL

#define SBA_FUNC_ID	0x0000	/* function id */
#define SBA_FCLASS	0x0008	/* function class, bist, header, rev... */

#define SBA_FUNC_SIZE 4096   /* SBA configuration function reg set */

#define ASTRO_IOC_OFFSET	(32 * SBA_FUNC_SIZE)
#define PLUTO_IOC_OFFSET	(1 * SBA_FUNC_SIZE)
/* Ike's IOC's occupy functions 2 and 3 */
#define IKE_IOC_OFFSET(p)	((p+2) * SBA_FUNC_SIZE)

#define IOC_CTRL          0x8	/* IOC_CTRL offset */
#define IOC_CTRL_TC       (1 << 0) /* TOC Enable */
#define IOC_CTRL_CE       (1 << 1) /* Coalesce Enable */
#define IOC_CTRL_DE       (1 << 2) /* Dillon Enable */
#define IOC_CTRL_RM       (1 << 8) /* Real Mode */
#define IOC_CTRL_NC       (1 << 9) /* Non Coherent Mode */
#define IOC_CTRL_D4       (1 << 11) /* Disable 4-byte coalescing */
#define IOC_CTRL_DD       (1 << 13) /* Disable distr. LMMIO range coalescing */

/*
** Offsets into MBIB (Function 0 on Ike and hopefully Astro)
** Firmware programs this stuff. Don't touch it.
*/
#define LMMIO_DIRECT0_BASE  0x300
#define LMMIO_DIRECT0_MASK  0x308
#define LMMIO_DIRECT0_ROUTE 0x310

#define LMMIO_DIST_BASE  0x360
#define LMMIO_DIST_MASK  0x368
#define LMMIO_DIST_ROUTE 0x370

#define IOS_DIST_BASE	0x390
#define IOS_DIST_MASK	0x398
#define IOS_DIST_ROUTE	0x3A0

#define IOS_DIRECT_BASE	0x3C0
#define IOS_DIRECT_MASK	0x3C8
#define IOS_DIRECT_ROUTE 0x3D0

/*
** Offsets into I/O TLB (Function 2 and 3 on Ike)
*/
#define ROPE0_CTL	0x200  /* "regbus pci0" */
#define ROPE1_CTL	0x208
#define ROPE2_CTL	0x210
#define ROPE3_CTL	0x218
#define ROPE4_CTL	0x220
#define ROPE5_CTL	0x228
#define ROPE6_CTL	0x230
#define ROPE7_CTL	0x238

#define IOC_ROPE0_CFG	0x500	/* pluto only */
#define   IOC_ROPE_AO	  0x10	/* Allow "Relaxed Ordering" */

#define HF_ENABLE	0x40

#define IOC_IBASE	0x300	/* IO TLB */
#define IOC_IMASK	0x308
#define IOC_PCOM	0x310
#define IOC_TCNFG	0x318
#define IOC_PDIR_BASE	0x320

/*
** IOC supports 4/8/16/64KB page sizes (see TCNFG register)
** It's safer (avoid memory corruption) to keep DMA page mappings
** equivalently sized to VM PAGE_SIZE.
**
** We really can't avoid generating a new mapping for each
** page since the Virtual Coherence Index has to be generated
** and updated for each page.
**
** PAGE_SIZE could be greater than IOVP_SIZE. But not the inverse.
*/
#define IOVP_SIZE	PAGE_SIZE
#define IOVP_SHIFT	PAGE_SHIFT
#define IOVP_MASK	PAGE_MASK

#define SBA_PERF_CFG	0x708	/* Performance Counter stuff */
#define SBA_PERF_MASK1	0x718
#define SBA_PERF_MASK2	0x730

/*
** Offsets into PCI Performance Counters (functions 12 and 13)
** Controlled by PERF registers in function 2 & 3 respectively.
*/
#define SBA_PERF_CNT1	0x200
#define SBA_PERF_CNT2	0x208
#define SBA_PERF_CNT3	0x210

/*
** lba_device: Per instance Elroy data structure
*/
struct lba_device {
	struct pci_hba_data	hba;

	spinlock_t		lba_lock;
	void			*iosapic_obj;

#ifdef CONFIG_64BIT
	void __iomem		*iop_base;	/* PA_VIEW - for IO port accessor funcs */
#endif

	int			flags;		/* state/functionality enabled */
	int			hw_rev;		/* HW revision of chip */
};

#define ELROY_HVERS		0x782
#define MERCURY_HVERS		0x783
#define QUICKSILVER_HVERS	0x784

static inline int IS_ELROY(struct parisc_device *d) {
	return (d->id.hversion == ELROY_HVERS);
}

static inline int IS_MERCURY(struct parisc_device *d) {
	return (d->id.hversion == MERCURY_HVERS);
}

static inline int IS_QUICKSILVER(struct parisc_device *d) {
	return (d->id.hversion == QUICKSILVER_HVERS);
}

static inline int agp_mode_mercury(void __iomem *hpa) {
	u64 bus_mode;

	bus_mode = readl(hpa + 0x0620);
	if (bus_mode & 1)
		return 1;

	return 0;
}

/*
** I/O SAPIC init function
** Caller knows where an I/O SAPIC is. LBA has an integrated I/O SAPIC.
** Call setup as part of per instance initialization.
** (ie *not* init_module() function unless only one is present.)
** fixup_irq is to initialize PCI IRQ line support and
** virtualize pcidev->irq value. To be called by pci_fixup_bus().
*/
extern void *iosapic_register(unsigned long hpa);
extern int iosapic_fixup_irq(void *obj, struct pci_dev *pcidev);

#define LBA_FUNC_ID	0x0000	/* function id */
#define LBA_FCLASS	0x0008	/* function class, bist, header, rev... */
#define LBA_CAPABLE	0x0030	/* capabilities register */

#define LBA_PCI_CFG_ADDR	0x0040	/* poke CFG address here */
#define LBA_PCI_CFG_DATA	0x0048	/* read or write data here */

#define LBA_PMC_MTLT	0x0050	/* Firmware sets this - read only. */
#define LBA_FW_SCRATCH	0x0058	/* Firmware writes the PCI bus number here. */
#define LBA_ERROR_ADDR	0x0070	/* On error, address gets logged here */

#define LBA_ARB_MASK	0x0080	/* bit 0 enable arbitration. PAT/PDC enables */
#define LBA_ARB_PRI	0x0088	/* firmware sets this. */
#define LBA_ARB_MODE	0x0090	/* firmware sets this. */
#define LBA_ARB_MTLT	0x0098	/* firmware sets this. */

#define LBA_MOD_ID	0x0100	/* Module ID. PDC_PAT_CELL reports 4 */

#define LBA_STAT_CTL	0x0108	/* Status & Control */
#define   LBA_BUS_RESET		0x01	/*  Deassert PCI Bus Reset Signal */
#define   CLEAR_ERRLOG		0x10	/*  "Clear Error Log" cmd */
#define   CLEAR_ERRLOG_ENABLE	0x20	/*  "Clear Error Log" Enable */
#define   HF_ENABLE	0x40	/*    enable HF mode (default is -1 mode) */

#define LBA_LMMIO_BASE	0x0200	/* < 4GB I/O address range */
#define LBA_LMMIO_MASK	0x0208

#define LBA_GMMIO_BASE	0x0210	/* > 4GB I/O address range */
#define LBA_GMMIO_MASK	0x0218

#define LBA_WLMMIO_BASE	0x0220	/* All < 4GB ranges under the same *SBA* */
#define LBA_WLMMIO_MASK	0x0228

#define LBA_WGMMIO_BASE	0x0230	/* All > 4GB ranges under the same *SBA* */
#define LBA_WGMMIO_MASK	0x0238

#define LBA_IOS_BASE	0x0240	/* I/O port space for this LBA */
#define LBA_IOS_MASK	0x0248

#define LBA_ELMMIO_BASE	0x0250	/* Extra LMMIO range */
#define LBA_ELMMIO_MASK	0x0258

#define LBA_EIOS_BASE	0x0260	/* Extra I/O port space */
#define LBA_EIOS_MASK	0x0268

#define LBA_GLOBAL_MASK	0x0270	/* Mercury only: Global Address Mask */
#define LBA_DMA_CTL	0x0278	/* firmware sets this */

#define LBA_IBASE	0x0300	/* SBA DMA support */
#define LBA_IMASK	0x0308

/* FIXME: ignore DMA Hint stuff until we can measure performance */
#define LBA_HINT_CFG	0x0310
#define LBA_HINT_BASE	0x0380	/* 14 registers at every 8 bytes. */

#define LBA_BUS_MODE	0x0620

/* ERROR regs are needed for config cycle kluges */
#define LBA_ERROR_CONFIG 0x0680
#define     LBA_SMART_MODE 0x20
#define LBA_ERROR_STATUS 0x0688
#define LBA_ROPE_CTL     0x06A0

#define LBA_IOSAPIC_BASE	0x800 /* Offset of IRQ logic */

#endif /*_ASM_PARISC_ROPES_H_*/
