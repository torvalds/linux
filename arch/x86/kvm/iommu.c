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
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Author: Allen M. Kay <allen.m.kay@intel.com>
 * Author: Weidong Han <weidong.han@intel.com>
 * Author: Ben-Ami Yassour <benami@il.ibm.com>
 */

#include <linux/list.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/stat.h>
#include <linux/dmar.h>
#include <linux/iommu.h>
#include <linux/intel-iommu.h>
#include "assigned-dev.h"

static bool allow_unsafe_assigned_interrupts;
module_param_named(allow_unsafe_assigned_interrupts,
		   allow_unsafe_assigned_interrupts, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(allow_unsafe_assigned_interrupts,
 "Enable device assignment on platforms without interrupt remapping support.");

static int kvm_iommu_unmap_memslots(struct kvm *kvm);
static void kvm_iommu_put_pages(struct kvm *kvm,
				gfn_t base_gfn, unsigned long npages);

static pfn_t kvm_pin_pages(struct kvm_memory_slot *slot, gfn_t gfn,
			   unsigned long npages)
{
	gfn_t end_gfn;
	pfn_t pfn;

	pfn     = gfn_to_pfn_memslot(slot, gfn);
	end_gfn = gfn + npages;
	gfn    += 1;

	if (is_error_noslot_pfn(pfn))
		return pfn;

	while (gfn < end_gfn)
		gfn_to_pfn_memslot(slot, gfn++);

	return pfn;
}

static void kvm_unpin_pages(struct kvm *kvm, pfn_t pfn, unsigned long npages)
{
	unsigned long i;

	for (i = 0; i < npages; ++i)
		kvm_release_pfn_clean(pfn + i);
}

int kvm_iommu_map_pages(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	gfn_t gfn, end_gfn;
	pfn_t pfn;
	int r = 0;
	struct iommu_domain *domain = kvm->arch.iommu_domain;
	int flags;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	gfn     = slot->base_gfn;
	end_gfn = gfn + slot->npages;

	flags = IOMMU_READ;
	if (!(slot->flags & KVM_MEM_READONLY))
		flags |= IOMMU_WRITE;
	if (!kvm->arch.iommu_noncoherent)
		flags |= IOMMU_CACHE;


	while (gfn < end_gfn) {
		unsigned long page_size;

		/* Check if already mapped */
		if (iommu_iova_to_phys(domain, gfn_to_gpa(gfn))) {
			gfn += 1;
			continue;
		}

		/* Get the page size we could use to map */
		page_size = kvm_host_page_size(kvm, gfn);

		/* Make sure the page_size does not exceed the memslot */
		while ((gfn + (page_size >> PAGE_SHIFT)) > end_gfn)
			page_size >>= 1;

		/* Make sure gfn is aligned to the page size we want to map */
		while ((gfn << PAGE_SHIFT) & (page_size - 1))
			page_size >>= 1;

		/* Make sure hva is aligned to the page size we want to map */
		while (__gfn_to_hva_memslot(slot, gfn) & (page_size - 1))
			page_size >>= 1;

		/*
		 * Pin all pages we are about to map in memory. This is
		 * important because we unmap and unpin in 4kb steps later.
		 */
		pfn = kvm_pin_pages(slot, gfn, page_size >> PAGE_SHIFT);
		if (is_error_noslot_pfn(pfn)) {
			gfn += 1;
			continue;
		}

		/* Map into IO address space */
		r = iommu_map(domain, gfn_to_gpa(gfn), pfn_to_hpa(pfn),
			      page_size, flags);
		if (r) {
			printk(KERN_ERR "kvm_iommu_map_address:"
			       "iommu failed to map pfn=%llx\n", pfn);
			kvm_unpin_pages(kvm, pfn, page_size >> PAGE_SHIFT);
			goto unmap_pages;
		}

		gfn += page_size >> PAGE_SHIFT;

		cond_resched();
	}

	return 0;

unmap_pages:
	kvm_iommu_put_pages(kvm, slot->base_gfn, gfn - slot->base_gfn);
	return r;
}

static int kvm_iommu_map_memslots(struct kvm *kvm)
{
	int idx, r = 0;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	if (kvm->arch.iommu_noncoherent)
		kvm_arch_register_noncoherent_dma(kvm);

	idx = srcu_read_lock(&kvm->srcu);
	slots = kvm_memslots(kvm);

	kvm_for_each_memslot(memslot, slots) {
		r = kvm_iommu_map_pages(kvm, memslot);
		if (r)
			break;
	}
	srcu_read_unlock(&kvm->srcu, idx);

	return r;
}

int kvm_assign_device(struct kvm *kvm, struct pci_dev *pdev)
{
	struct iommu_domain *domain = kvm->arch.iommu_domain;
	int r;
	bool noncoherent;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	if (pdev == NULL)
		return -ENODEV;

	r = iommu_attach_device(domain, &pdev->dev);
	if (r) {
		dev_err(&pdev->dev, "kvm assign device failed ret %d", r);
		return r;
	}

	noncoherent = !iommu_capable(&pci_bus_type, IOMMU_CAP_CACHE_COHERENCY);

	/* Check if need to update IOMMU page table for guest memory */
	if (noncoherent != kvm->arch.iommu_noncoherent) {
		kvm_iommu_unmap_memslots(kvm);
		kvm->arch.iommu_noncoherent = noncoherent;
		r = kvm_iommu_map_memslots(kvm);
		if (r)
			goto out_unmap;
	}

	pci_set_dev_assigned(pdev);

	dev_info(&pdev->dev, "kvm assign device\n");

	return 0;
out_unmap:
	kvm_iommu_unmap_memslots(kvm);
	return r;
}

int kvm_deassign_device(struct kvm *kvm, struct pci_dev *pdev)
{
	struct iommu_domain *domain = kvm->arch.iommu_domain;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	if (pdev == NULL)
		return -ENODEV;

	iommu_detach_device(domain, &pdev->dev);

	pci_clear_dev_assigned(pdev);

	dev_info(&pdev->dev, "kvm deassign device\n");

	return 0;
}

int kvm_iommu_map_guest(struct kvm *kvm)
{
	int r;

	if (!iommu_present(&pci_bus_type)) {
		printk(KERN_ERR "%s: iommu not found\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&kvm->slots_lock);

	kvm->arch.iommu_domain = iommu_domain_alloc(&pci_bus_type);
	if (!kvm->arch.iommu_domain) {
		r = -ENOMEM;
		goto out_unlock;
	}

	if (!allow_unsafe_assigned_interrupts &&
	    !iommu_capable(&pci_bus_type, IOMMU_CAP_INTR_REMAP)) {
		printk(KERN_WARNING "%s: No interrupt remapping support,"
		       " disallowing device assignment."
		       " Re-enble with \"allow_unsafe_assigned_interrupts=1\""
		       " module option.\n", __func__);
		iommu_domain_free(kvm->arch.iommu_domain);
		kvm->arch.iommu_domain = NULL;
		r = -EPERM;
		goto out_unlock;
	}

	r = kvm_iommu_map_memslots(kvm);
	if (r)
		kvm_iommu_unmap_memslots(kvm);

out_unlock:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static void kvm_iommu_put_pages(struct kvm *kvm,
				gfn_t base_gfn, unsigned long npages)
{
	struct iommu_domain *domain;
	gfn_t end_gfn, gfn;
	pfn_t pfn;
	u64 phys;

	domain  = kvm->arch.iommu_domain;
	end_gfn = base_gfn + npages;
	gfn     = base_gfn;

	/* check if iommu exists and in use */
	if (!domain)
		return;

	while (gfn < end_gfn) {
		unsigned long unmap_pages;
		size_t size;

		/* Get physical address */
		phys = iommu_iova_to_phys(domain, gfn_to_gpa(gfn));

		if (!phys) {
			gfn++;
			continue;
		}

		pfn  = phys >> PAGE_SHIFT;

		/* Unmap address from IO address space */
		size       = iommu_unmap(domain, gfn_to_gpa(gfn), PAGE_SIZE);
		unmap_pages = 1ULL << get_order(size);

		/* Unpin all pages we just unmapped to not leak any memory */
		kvm_unpin_pages(kvm, pfn, unmap_pages);

		gfn += unmap_pages;

		cond_resched();
	}
}

void kvm_iommu_unmap_pages(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	kvm_iommu_put_pages(kvm, slot->base_gfn, slot->npages);
}

static int kvm_iommu_unmap_memslots(struct kvm *kvm)
{
	int idx;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	idx = srcu_read_lock(&kvm->srcu);
	slots = kvm_memslots(kvm);

	kvm_for_each_memslot(memslot, slots)
		kvm_iommu_unmap_pages(kvm, memslot);

	srcu_read_unlock(&kvm->srcu, idx);

	if (kvm->arch.iommu_noncoherent)
		kvm_arch_unregister_noncoherent_dma(kvm);

	return 0;
}

int kvm_iommu_unmap_guest(struct kvm *kvm)
{
	struct iommu_domain *domain = kvm->arch.iommu_domain;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	mutex_lock(&kvm->slots_lock);
	kvm_iommu_unmap_memslots(kvm);
	kvm->arch.iommu_domain = NULL;
	kvm->arch.iommu_noncoherent = false;
	mutex_unlock(&kvm->slots_lock);

	iommu_domain_free(domain);
	return 0;
}
