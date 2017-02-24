/*
 * Copyright (C) 2009-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _ASM_X86_AMD_IOMMU_PROTO_H
#define _ASM_X86_AMD_IOMMU_PROTO_H

#include "amd_iommu_types.h"

extern int amd_iommu_get_num_iommus(void);
extern int amd_iommu_init_dma_ops(void);
extern int amd_iommu_init_passthrough(void);
extern irqreturn_t amd_iommu_int_thread(int irq, void *data);
extern irqreturn_t amd_iommu_int_handler(int irq, void *data);
extern void amd_iommu_apply_erratum_63(u16 devid);
extern void amd_iommu_reset_cmd_buffer(struct amd_iommu *iommu);
extern int amd_iommu_init_devices(void);
extern void amd_iommu_uninit_devices(void);
extern void amd_iommu_init_notifier(void);
extern int amd_iommu_init_api(void);

/* Needed for interrupt remapping */
extern int amd_iommu_prepare(void);
extern int amd_iommu_enable(void);
extern void amd_iommu_disable(void);
extern int amd_iommu_reenable(int);
extern int amd_iommu_enable_faulting(void);
extern int amd_iommu_guest_ir;

/* IOMMUv2 specific functions */
struct iommu_domain;

extern bool amd_iommu_v2_supported(void);
extern int amd_iommu_register_ppr_notifier(struct notifier_block *nb);
extern int amd_iommu_unregister_ppr_notifier(struct notifier_block *nb);
extern void amd_iommu_domain_direct_map(struct iommu_domain *dom);
extern int amd_iommu_domain_enable_v2(struct iommu_domain *dom, int pasids);
extern int amd_iommu_flush_page(struct iommu_domain *dom, int pasid,
				u64 address);
extern int amd_iommu_flush_tlb(struct iommu_domain *dom, int pasid);
extern int amd_iommu_domain_set_gcr3(struct iommu_domain *dom, int pasid,
				     unsigned long cr3);
extern int amd_iommu_domain_clear_gcr3(struct iommu_domain *dom, int pasid);
extern struct iommu_domain *amd_iommu_get_v2_domain(struct pci_dev *pdev);

#ifdef CONFIG_IRQ_REMAP
extern int amd_iommu_create_irq_domain(struct amd_iommu *iommu);
#else
static inline int amd_iommu_create_irq_domain(struct amd_iommu *iommu)
{
	return 0;
}
#endif

#define PPR_SUCCESS			0x0
#define PPR_INVALID			0x1
#define PPR_FAILURE			0xf

extern int amd_iommu_complete_ppr(struct pci_dev *pdev, int pasid,
				  int status, int tag);

static inline bool is_rd890_iommu(struct pci_dev *pdev)
{
	return (pdev->vendor == PCI_VENDOR_ID_ATI) &&
	       (pdev->device == PCI_DEVICE_ID_RD890_IOMMU);
}

static inline bool iommu_feature(struct amd_iommu *iommu, u64 f)
{
	if (!(iommu->cap & (1 << IOMMU_CAP_EFR)))
		return false;

	return !!(iommu->features & f);
}

#endif /* _ASM_X86_AMD_IOMMU_PROTO_H  */
