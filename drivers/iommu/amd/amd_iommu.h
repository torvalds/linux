/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#ifndef AMD_IOMMU_H
#define AMD_IOMMU_H

#include <linux/iommu.h>

#include "amd_iommu_types.h"

irqreturn_t amd_iommu_int_thread(int irq, void *data);
irqreturn_t amd_iommu_int_handler(int irq, void *data);
void amd_iommu_apply_erratum_63(struct amd_iommu *iommu, u16 devid);
void amd_iommu_restart_event_logging(struct amd_iommu *iommu);
void amd_iommu_restart_ga_log(struct amd_iommu *iommu);
int amd_iommu_init_devices(void);
void amd_iommu_uninit_devices(void);
void amd_iommu_init_notifier(void);
void amd_iommu_set_rlookup_table(struct amd_iommu *iommu, u16 devid);

#ifdef CONFIG_AMD_IOMMU_DEBUGFS
void amd_iommu_debugfs_setup(struct amd_iommu *iommu);
#else
static inline void amd_iommu_debugfs_setup(struct amd_iommu *iommu) {}
#endif

/* Needed for interrupt remapping */
int amd_iommu_prepare(void);
int amd_iommu_enable(void);
void amd_iommu_disable(void);
int amd_iommu_reenable(int mode);
int amd_iommu_enable_faulting(void);
extern int amd_iommu_guest_ir;
extern enum io_pgtable_fmt amd_iommu_pgtable;
extern int amd_iommu_gpt_level;

/* IOMMUv2 specific functions */
struct iommu_domain;

bool amd_iommu_v2_supported(void);
struct amd_iommu *get_amd_iommu(unsigned int idx);
u8 amd_iommu_pc_get_max_banks(unsigned int idx);
bool amd_iommu_pc_supported(void);
u8 amd_iommu_pc_get_max_counters(unsigned int idx);
int amd_iommu_pc_get_reg(struct amd_iommu *iommu, u8 bank, u8 cntr,
			 u8 fxn, u64 *value);
int amd_iommu_pc_set_reg(struct amd_iommu *iommu, u8 bank, u8 cntr,
			 u8 fxn, u64 *value);

int amd_iommu_register_ppr_notifier(struct notifier_block *nb);
int amd_iommu_unregister_ppr_notifier(struct notifier_block *nb);
void amd_iommu_domain_direct_map(struct iommu_domain *dom);
int amd_iommu_domain_enable_v2(struct iommu_domain *dom, int pasids);
int amd_iommu_flush_page(struct iommu_domain *dom, u32 pasid, u64 address);
void amd_iommu_update_and_flush_device_table(struct protection_domain *domain);
void amd_iommu_domain_update(struct protection_domain *domain);
void amd_iommu_domain_flush_complete(struct protection_domain *domain);
void amd_iommu_domain_flush_tlb_pde(struct protection_domain *domain);
int amd_iommu_flush_tlb(struct iommu_domain *dom, u32 pasid);
int amd_iommu_domain_set_gcr3(struct iommu_domain *dom, u32 pasid,
			      unsigned long cr3);
int amd_iommu_domain_clear_gcr3(struct iommu_domain *dom, u32 pasid);

#ifdef CONFIG_IRQ_REMAP
int amd_iommu_create_irq_domain(struct amd_iommu *iommu);
#else
static inline int amd_iommu_create_irq_domain(struct amd_iommu *iommu)
{
	return 0;
}
#endif

#define PPR_SUCCESS			0x0
#define PPR_INVALID			0x1
#define PPR_FAILURE			0xf

int amd_iommu_complete_ppr(struct pci_dev *pdev, u32 pasid,
			   int status, int tag);

static inline bool is_rd890_iommu(struct pci_dev *pdev)
{
	return (pdev->vendor == PCI_VENDOR_ID_ATI) &&
	       (pdev->device == PCI_DEVICE_ID_RD890_IOMMU);
}

static inline bool iommu_feature(struct amd_iommu *iommu, u64 mask)
{
	return !!(iommu->features & mask);
}

static inline u64 iommu_virt_to_phys(void *vaddr)
{
	return (u64)__sme_set(virt_to_phys(vaddr));
}

static inline void *iommu_phys_to_virt(unsigned long paddr)
{
	return phys_to_virt(__sme_clr(paddr));
}

static inline
void amd_iommu_domain_set_pt_root(struct protection_domain *domain, u64 root)
{
	atomic64_set(&domain->iop.pt_root, root);
	domain->iop.root = (u64 *)(root & PAGE_MASK);
	domain->iop.mode = root & 7; /* lowest 3 bits encode pgtable mode */
}

static inline
void amd_iommu_domain_clr_pt_root(struct protection_domain *domain)
{
	amd_iommu_domain_set_pt_root(domain, 0);
}

static inline int get_pci_sbdf_id(struct pci_dev *pdev)
{
	int seg = pci_domain_nr(pdev->bus);
	u16 devid = pci_dev_id(pdev);

	return PCI_SEG_DEVID_TO_SBDF(seg, devid);
}

static inline void *alloc_pgtable_page(int nid, gfp_t gfp)
{
	struct page *page;

	page = alloc_pages_node(nid, gfp | __GFP_ZERO, 0);
	return page ? page_address(page) : NULL;
}

bool translation_pre_enabled(struct amd_iommu *iommu);
bool amd_iommu_is_attach_deferred(struct device *dev);
int __init add_special_device(u8 type, u8 id, u32 *devid, bool cmd_line);

#ifdef CONFIG_DMI
void amd_iommu_apply_ivrs_quirks(void);
#else
static inline void amd_iommu_apply_ivrs_quirks(void) { }
#endif

void amd_iommu_domain_set_pgtable(struct protection_domain *domain,
				  u64 *root, int mode);
struct dev_table_entry *get_dev_table(struct amd_iommu *iommu);

extern u64 amd_iommu_efr;
extern u64 amd_iommu_efr2;

extern bool amd_iommu_snp_en;
#endif
