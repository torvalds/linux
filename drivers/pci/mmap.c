// SPDX-License-Identifier: GPL-2.0
/*
 * Generic PCI resource mmap helper
 *
 * Copyright © 2017 Amazon.com, Inc. or its affiliates.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>

#ifdef ARCH_GENERIC_PCI_MMAP_RESOURCE

static const struct vm_operations_struct pci_phys_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

int pci_mmap_resource_range(struct pci_dev *pdev, int bar,
			    struct vm_area_struct *vma,
			    enum pci_mmap_state mmap_state, int write_combine)
{
	unsigned long size;
	int ret;

	size = ((pci_resource_len(pdev, bar) - 1) >> PAGE_SHIFT) + 1;
	if (vma->vm_pgoff + vma_pages(vma) > size)
		return -EINVAL;

	if (write_combine)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_device(vma->vm_page_prot);

	if (mmap_state == pci_mmap_io) {
		ret = pci_iobar_pfn(pdev, bar, vma);
		if (ret)
			return ret;
	} else
		vma->vm_pgoff += (pci_resource_start(pdev, bar) >> PAGE_SHIFT);

	vma->vm_ops = &pci_phys_vm_ops;

	return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				  vma->vm_end - vma->vm_start,
				  vma->vm_page_prot);
}

#endif
