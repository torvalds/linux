/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright 2017 IBM Corp.
#ifndef _ASM_PNV_OCXL_H
#define _ASM_PNV_OCXL_H

#include <linux/bitfield.h>
#include <linux/pci.h>

#define PNV_OCXL_TL_MAX_TEMPLATE        63
#define PNV_OCXL_TL_BITS_PER_RATE       4
#define PNV_OCXL_TL_RATE_BUF_SIZE       ((PNV_OCXL_TL_MAX_TEMPLATE+1) * PNV_OCXL_TL_BITS_PER_RATE / 8)

#define PNV_OCXL_ATSD_TIMEOUT		1

/* TLB Management Instructions */
#define PNV_OCXL_ATSD_LNCH		0x00
/* Radix Invalidate */
#define   PNV_OCXL_ATSD_LNCH_R		PPC_BIT(0)
/* Radix Invalidation Control
 * 0b00 Just invalidate TLB.
 * 0b01 Invalidate just Page Walk Cache.
 * 0b10 Invalidate TLB, Page Walk Cache, and any
 * caching of Partition and Process Table Entries.
 */
#define   PNV_OCXL_ATSD_LNCH_RIC	PPC_BITMASK(1, 2)
/* Number and Page Size of translations to be invalidated */
#define   PNV_OCXL_ATSD_LNCH_LP		PPC_BITMASK(3, 10)
/* Invalidation Criteria
 * 0b00 Invalidate just the target VA.
 * 0b01 Invalidate matching PID.
 */
#define   PNV_OCXL_ATSD_LNCH_IS		PPC_BITMASK(11, 12)
/* 0b1: Process Scope, 0b0: Partition Scope */
#define   PNV_OCXL_ATSD_LNCH_PRS	PPC_BIT(13)
/* Invalidation Flag */
#define   PNV_OCXL_ATSD_LNCH_B		PPC_BIT(14)
/* Actual Page Size to be invalidated
 * 000 4KB
 * 101 64KB
 * 001 2MB
 * 010 1GB
 */
#define   PNV_OCXL_ATSD_LNCH_AP		PPC_BITMASK(15, 17)
/* Defines the large page select
 * L=0b0 for 4KB pages
 * L=0b1 for large pages)
 */
#define   PNV_OCXL_ATSD_LNCH_L		PPC_BIT(18)
/* Process ID */
#define   PNV_OCXL_ATSD_LNCH_PID	PPC_BITMASK(19, 38)
/* NoFlush – Assumed to be 0b0 */
#define   PNV_OCXL_ATSD_LNCH_F		PPC_BIT(39)
#define   PNV_OCXL_ATSD_LNCH_OCAPI_SLBI	PPC_BIT(40)
#define   PNV_OCXL_ATSD_LNCH_OCAPI_SINGLETON	PPC_BIT(41)
#define PNV_OCXL_ATSD_AVA		0x08
#define   PNV_OCXL_ATSD_AVA_AVA		PPC_BITMASK(0, 51)
#define PNV_OCXL_ATSD_STAT		0x10

int pnv_ocxl_get_actag(struct pci_dev *dev, u16 *base, u16 *enabled, u16 *supported);
int pnv_ocxl_get_pasid_count(struct pci_dev *dev, int *count);

int pnv_ocxl_get_tl_cap(struct pci_dev *dev, long *cap,
			char *rate_buf, int rate_buf_size);
int pnv_ocxl_set_tl_conf(struct pci_dev *dev, long cap,
			 uint64_t rate_buf_phys, int rate_buf_size);

int pnv_ocxl_get_xsl_irq(struct pci_dev *dev, int *hwirq);
void pnv_ocxl_unmap_xsl_regs(void __iomem *dsisr, void __iomem *dar,
			     void __iomem *tfc, void __iomem *pe_handle);
int pnv_ocxl_map_xsl_regs(struct pci_dev *dev, void __iomem **dsisr,
			  void __iomem **dar, void __iomem **tfc,
			  void __iomem **pe_handle);

int pnv_ocxl_spa_setup(struct pci_dev *dev, void *spa_mem, int PE_mask, void **platform_data);
void pnv_ocxl_spa_release(void *platform_data);
int pnv_ocxl_spa_remove_pe_from_cache(void *platform_data, int pe_handle);

int pnv_ocxl_map_lpar(struct pci_dev *dev, uint64_t lparid,
		      uint64_t lpcr, void __iomem **arva);
void pnv_ocxl_unmap_lpar(void __iomem *arva);
void pnv_ocxl_tlb_invalidate(void __iomem *arva,
			     unsigned long pid,
			     unsigned long addr,
			     unsigned long page_size);
#endif /* _ASM_PNV_OCXL_H */
