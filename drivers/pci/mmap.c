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

/*
 * Modern setup: generic pci_mmap_resource_range(), and implement the legacy
 * pci_mmap_page_range() (if needed) as a wrapper round it.
 */

#ifdef HAVE_PCI_MMAP
int pci_mmap_page_range(struct pci_dev *pdev, int bar,
			struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	resource_size_t start, end;

	pci_resource_to_user(pdev, bar, &pdev->resource[bar], &start, &end);

	/* Adjust vm_pgoff to be the offset within the resource */
	vma->vm_pgoff -= start >> PAGE_SHIFT;
	return pci_mmap_resource_range(pdev, bar, vma, mmap_state,
				       write_combine);
}
#endif

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

#elif defined(HAVE_PCI_MMAP) /* && !ARCH_GENERIC_PCI_MMAP_RESOURCE */

/*
 * Legacy setup: Implement pci_mmap_resource_range() as a wrapper around
 * the architecture's pci_mmap_page_range(), converting to "user visible"
 * addresses as necessary.
 */

int pci_mmap_resource_range(struct pci_dev *pdev, int bar,
			    struct vm_area_struct *vma,
			    enum pci_mmap_state mmap_state, int write_combine)
{
	resource_size_t start, end;

	/*
	 * pci_mmap_page_range() expects the same kind of entry as coming
	 * from /proc/bus/pci/ which is a "user visible" value. If this is
	 * different from the resource itself, arch will do necessary fixup.
	 */
	pci_resource_to_user(pdev, bar, &pdev->resource[bar], &start, &end);
	vma->vm_pgoff += start >> PAGE_SHIFT;
	return pci_mmap_page_range(pdev, bar, vma, mmap_state, write_combine);
}
#endif
