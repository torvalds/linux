/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Copyright IBM Corporation, 2008
 * Author: Allen M. Kay <allen.m.kay@intel.com>
 * Author: Weidong Han <weidong.han@intel.com>
 * Author: Ben-Ami Yassour <benami@il.ibm.com>
 */

#include <linux/list.h>
#include <linux/kvm_host.h>
#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/intel-iommu.h>

static int kvm_iommu_unmap_memslots(struct kvm *kvm);
static void kvm_iommu_put_pages(struct kvm *kvm,
				gfn_t base_gfn, unsigned long npages);

int kvm_iommu_map_pages(struct kvm *kvm,
			gfn_t base_gfn, unsigned long npages)
{
	gfn_t gfn = base_gfn;
	pfn_t pfn;
	int i, r = 0;
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	for (i = 0; i < npages; i++) {
		/* check if already mapped */
		pfn = (pfn_t)intel_iommu_iova_to_pfn(domain,
						     gfn_to_gpa(gfn));
		if (pfn)
			continue;

		pfn = gfn_to_pfn(kvm, gfn);
		r = intel_iommu_page_mapping(domain,
					     gfn_to_gpa(gfn),
					     pfn_to_hpa(pfn),
					     PAGE_SIZE,
					     DMA_PTE_READ |
					     DMA_PTE_WRITE);
		if (r) {
			printk(KERN_ERR "kvm_iommu_map_pages:"
			       "iommu failed to map pfn=%lx\n", pfn);
			goto unmap_pages;
		}
		gfn++;
	}
	return 0;

unmap_pages:
	kvm_iommu_put_pages(kvm, base_gfn, i);
	return r;
}

static int kvm_iommu_map_memslots(struct kvm *kvm)
{
	int i, r;

	down_read(&kvm->slots_lock);
	for (i = 0; i < kvm->nmemslots; i++) {
		r = kvm_iommu_map_pages(kvm, kvm->memslots[i].base_gfn,
					kvm->memslots[i].npages);
		if (r)
			break;
	}
	up_read(&kvm->slots_lock);
	return r;
}

int kvm_iommu_map_guest(struct kvm *kvm,
			struct kvm_assigned_dev_kernel *assigned_dev)
{
	struct pci_dev *pdev = NULL;
	int r;

	if (!intel_iommu_found()) {
		printk(KERN_ERR "%s: intel iommu not found\n", __func__);
		return -ENODEV;
	}

	printk(KERN_DEBUG "VT-d direct map: host bdf = %x:%x:%x\n",
	       assigned_dev->host_busnr,
	       PCI_SLOT(assigned_dev->host_devfn),
	       PCI_FUNC(assigned_dev->host_devfn));

	pdev = assigned_dev->dev;

	if (pdev == NULL) {
		if (kvm->arch.intel_iommu_domain) {
			intel_iommu_domain_exit(kvm->arch.intel_iommu_domain);
			kvm->arch.intel_iommu_domain = NULL;
		}
		return -ENODEV;
	}

	kvm->arch.intel_iommu_domain = intel_iommu_domain_alloc(pdev);
	if (!kvm->arch.intel_iommu_domain)
		return -ENODEV;

	r = kvm_iommu_map_memslots(kvm);
	if (r)
		goto out_unmap;

	intel_iommu_detach_dev(kvm->arch.intel_iommu_domain,
			       pdev->bus->number, pdev->devfn);

	r = intel_iommu_context_mapping(kvm->arch.intel_iommu_domain,
					pdev);
	if (r) {
		printk(KERN_ERR "Domain context map for %s failed",
		       pci_name(pdev));
		goto out_unmap;
	}
	return 0;

out_unmap:
	kvm_iommu_unmap_memslots(kvm);
	return r;
}

static void kvm_iommu_put_pages(struct kvm *kvm,
			       gfn_t base_gfn, unsigned long npages)
{
	gfn_t gfn = base_gfn;
	pfn_t pfn;
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;
	int i;

	for (i = 0; i < npages; i++) {
		pfn = (pfn_t)intel_iommu_iova_to_pfn(domain,
						     gfn_to_gpa(gfn));
		kvm_release_pfn_clean(pfn);
		gfn++;
	}
}

static int kvm_iommu_unmap_memslots(struct kvm *kvm)
{
	int i;
	down_read(&kvm->slots_lock);
	for (i = 0; i < kvm->nmemslots; i++) {
		kvm_iommu_put_pages(kvm, kvm->memslots[i].base_gfn,
				    kvm->memslots[i].npages);
	}
	up_read(&kvm->slots_lock);

	return 0;
}

int kvm_iommu_unmap_guest(struct kvm *kvm)
{
	struct kvm_assigned_dev_kernel *entry;
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	list_for_each_entry(entry, &kvm->arch.assigned_dev_head, list) {
		printk(KERN_DEBUG "VT-d unmap: host bdf = %x:%x:%x\n",
		       entry->host_busnr,
		       PCI_SLOT(entry->host_devfn),
		       PCI_FUNC(entry->host_devfn));

		/* detach kvm dmar domain */
		intel_iommu_detach_dev(domain, entry->host_busnr,
				       entry->host_devfn);
	}
	kvm_iommu_unmap_memslots(kvm);
	intel_iommu_domain_exit(domain);
	return 0;
}
