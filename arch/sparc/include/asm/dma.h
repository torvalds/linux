/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SPARC_DMA_H
#define _ASM_SPARC_DMA_H

/* These are irrelevant for Sparc DMA, but we leave it in so that
 * things can compile.
 */
#define MAX_DMA_CHANNELS 8
#define DMA_MODE_READ    1
#define DMA_MODE_WRITE   2
#define MAX_DMA_ADDRESS  (~0UL)

/* Useful constants */
#define SIZE_16MB      (16*1024*1024)
#define SIZE_64K       (64*1024)

/* SBUS DMA controller reg offsets */
#define DMA_CSR		0x00UL		/* rw  DMA control/status register    0x00   */
#define DMA_ADDR	0x04UL		/* rw  DMA transfer address register  0x04   */
#define DMA_COUNT	0x08UL		/* rw  DMA transfer count register    0x08   */
#define DMA_TEST	0x0cUL		/* rw  DMA test/debug register        0x0c   */

/* Fields in the cond_reg register */
/* First, the version identification bits */
#define DMA_DEVICE_ID    0xf0000000        /* Device identification bits */
#define DMA_VERS0        0x00000000        /* Sunray DMA version */
#define DMA_ESCV1        0x40000000        /* DMA ESC Version 1 */
#define DMA_VERS1        0x80000000        /* DMA rev 1 */
#define DMA_VERS2        0xa0000000        /* DMA rev 2 */
#define DMA_VERHME       0xb0000000        /* DMA hme gate array */
#define DMA_VERSPLUS     0x90000000        /* DMA rev 1 PLUS */

#define DMA_HNDL_INTR    0x00000001        /* An IRQ needs to be handled */
#define DMA_HNDL_ERROR   0x00000002        /* We need to take an error */
#define DMA_FIFO_ISDRAIN 0x0000000c        /* The DMA FIFO is draining */
#define DMA_INT_ENAB     0x00000010        /* Turn on interrupts */
#define DMA_FIFO_INV     0x00000020        /* Invalidate the FIFO */
#define DMA_ACC_SZ_ERR   0x00000040        /* The access size was bad */
#define DMA_FIFO_STDRAIN 0x00000040        /* DMA_VERS1 Drain the FIFO */
#define DMA_RST_SCSI     0x00000080        /* Reset the SCSI controller */
#define DMA_RST_ENET     DMA_RST_SCSI      /* Reset the ENET controller */
#define DMA_ST_WRITE     0x00000100        /* write from device to memory */
#define DMA_ENABLE       0x00000200        /* Fire up DMA, handle requests */
#define DMA_PEND_READ    0x00000400        /* DMA_VERS1/0/PLUS Pending Read */
#define DMA_ESC_BURST    0x00000800        /* 1=16byte 0=32byte */
#define DMA_READ_AHEAD   0x00001800        /* DMA read ahead partial longword */
#define DMA_DSBL_RD_DRN  0x00001000        /* No EC drain on slave reads */
#define DMA_BCNT_ENAB    0x00002000        /* If on, use the byte counter */
#define DMA_TERM_CNTR    0x00004000        /* Terminal counter */
#define DMA_SCSI_SBUS64  0x00008000        /* HME: Enable 64-bit SBUS mode. */
#define DMA_CSR_DISAB    0x00010000        /* No FIFO drains during csr */
#define DMA_SCSI_DISAB   0x00020000        /* No FIFO drains during reg */
#define DMA_DSBL_WR_INV  0x00020000        /* No EC inval. on slave writes */
#define DMA_ADD_ENABLE   0x00040000        /* Special ESC DVMA optimization */
#define DMA_E_BURSTS	 0x000c0000	   /* ENET: SBUS r/w burst mask */
#define DMA_E_BURST32	 0x00040000	   /* ENET: SBUS 32 byte r/w burst */
#define DMA_E_BURST16	 0x00000000	   /* ENET: SBUS 16 byte r/w burst */
#define DMA_BRST_SZ      0x000c0000        /* SCSI: SBUS r/w burst size */
#define DMA_BRST64       0x000c0000        /* SCSI: 64byte bursts (HME on UltraSparc only) */
#define DMA_BRST32       0x00040000        /* SCSI: 32byte bursts */
#define DMA_BRST16       0x00000000        /* SCSI: 16byte bursts */
#define DMA_BRST0        0x00080000        /* SCSI: no bursts (non-HME gate arrays) */
#define DMA_ADDR_DISAB   0x00100000        /* No FIFO drains during addr */
#define DMA_2CLKS        0x00200000        /* Each transfer = 2 clock ticks */
#define DMA_3CLKS        0x00400000        /* Each transfer = 3 clock ticks */
#define DMA_EN_ENETAUI   DMA_3CLKS         /* Put lance into AUI-cable mode */
#define DMA_CNTR_DISAB   0x00800000        /* No IRQ when DMA_TERM_CNTR set */
#define DMA_AUTO_NADDR   0x01000000        /* Use "auto nxt addr" feature */
#define DMA_SCSI_ON      0x02000000        /* Enable SCSI dma */
#define DMA_PARITY_OFF   0x02000000        /* HME: disable parity checking */
#define DMA_LOADED_ADDR  0x04000000        /* Address has been loaded */
#define DMA_LOADED_NADDR 0x08000000        /* Next address has been loaded */
#define DMA_RESET_FAS366 0x08000000        /* HME: Assert RESET to FAS366 */

/* Values describing the burst-size property from the PROM */
#define DMA_BURST1       0x01
#define DMA_BURST2       0x02
#define DMA_BURST4       0x04
#define DMA_BURST8       0x08
#define DMA_BURST16      0x10
#define DMA_BURST32      0x20
#define DMA_BURST64      0x40
#define DMA_BURSTBITS    0x7f

/* From PCI */

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif

#ifdef CONFIG_SPARC32

/* Routines for data transfer buffers. */
struct device;
struct scatterlist;

struct sparc32_dma_ops {
	__u32 (*get_scsi_one)(struct device *, char *, unsigned long);
	void (*get_scsi_sgl)(struct device *, struct scatterlist *, int);
	void (*release_scsi_one)(struct device *, __u32, unsigned long);
	void (*release_scsi_sgl)(struct device *, struct scatterlist *,int);
#ifdef CONFIG_SBUS
	int (*map_dma_area)(struct device *, dma_addr_t *, unsigned long, unsigned long, int);
	void (*unmap_dma_area)(struct device *, unsigned long, int);
#endif
};
extern const struct sparc32_dma_ops *sparc32_dma_ops;

#define mmu_get_scsi_one(dev,vaddr,len) \
	sparc32_dma_ops->get_scsi_one(dev, vaddr, len)
#define mmu_get_scsi_sgl(dev,sg,sz) \
	sparc32_dma_ops->get_scsi_sgl(dev, sg, sz)
#define mmu_release_scsi_one(dev,vaddr,len) \
	sparc32_dma_ops->release_scsi_one(dev, vaddr,len)
#define mmu_release_scsi_sgl(dev,sg,sz) \
	sparc32_dma_ops->release_scsi_sgl(dev, sg, sz)

#ifdef CONFIG_SBUS
/*
 * mmu_map/unmap are provided by iommu/iounit; Invalid to call on IIep.
 *
 * The mmu_map_dma_area establishes two mappings in one go.
 * These mappings point to pages normally mapped at 'va' (linear address).
 * First mapping is for CPU visible address at 'a', uncached.
 * This is an alias, but it works because it is an uncached mapping.
 * Second mapping is for device visible address, or "bus" address.
 * The bus address is returned at '*pba'.
 *
 * These functions seem distinct, but are hard to split.
 * On sun4m, page attributes depend on the CPU type, so we have to
 * know if we are mapping RAM or I/O, so it has to be an additional argument
 * to a separate mapping function for CPU visible mappings.
 */
#define sbus_map_dma_area(dev,pba,va,a,len) \
	sparc32_dma_ops->map_dma_area(dev, pba, va, a, len)
#define sbus_unmap_dma_area(dev,ba,len) \
	sparc32_dma_ops->unmap_dma_area(dev, ba, len)
#endif /* CONFIG_SBUS */

#endif

#endif /* !(_ASM_SPARC_DMA_H) */
