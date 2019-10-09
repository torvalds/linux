// SPDX-License-Identifier: GPL-2.0
/**
 * PCI Endpoint *Controller* Address Space Management
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/pci-epc.h>

/**
 * pci_epc_mem_get_order() - determine the allocation order of a memory size
 * @mem: address space of the endpoint controller
 * @size: the size for which to get the order
 *
 * Reimplement get_order() for mem->page_size since the generic get_order
 * always gets order with a constant PAGE_SIZE.
 */
static int pci_epc_mem_get_order(struct pci_epc_mem *mem, size_t size)
{
	int order;
	unsigned int page_shift = ilog2(mem->page_size);

	size--;
	size >>= page_shift;
#if BITS_PER_LONG == 32
	order = fls(size);
#else
	order = fls64(size);
#endif
	return order;
}

/**
 * __pci_epc_mem_init() - initialize the pci_epc_mem structure
 * @epc: the EPC device that invoked pci_epc_mem_init
 * @phys_base: the physical address of the base
 * @size: the size of the address space
 * @page_size: size of each page
 *
 * Invoke to initialize the pci_epc_mem structure used by the
 * endpoint functions to allocate mapped PCI address.
 */
int __pci_epc_mem_init(struct pci_epc *epc, phys_addr_t phys_base, size_t size,
		       size_t page_size)
{
	int ret;
	struct pci_epc_mem *mem;
	unsigned long *bitmap;
	unsigned int page_shift;
	int pages;
	int bitmap_size;

	if (page_size < PAGE_SIZE)
		page_size = PAGE_SIZE;

	page_shift = ilog2(page_size);
	pages = size >> page_shift;
	bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem) {
		ret = -ENOMEM;
		goto err;
	}

	bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!bitmap) {
		ret = -ENOMEM;
		goto err_mem;
	}

	mem->bitmap = bitmap;
	mem->phys_base = phys_base;
	mem->page_size = page_size;
	mem->pages = pages;
	mem->size = size;

	epc->mem = mem;

	return 0;

err_mem:
	kfree(mem);

err:
return ret;
}
EXPORT_SYMBOL_GPL(__pci_epc_mem_init);

/**
 * pci_epc_mem_exit() - cleanup the pci_epc_mem structure
 * @epc: the EPC device that invoked pci_epc_mem_exit
 *
 * Invoke to cleanup the pci_epc_mem structure allocated in
 * pci_epc_mem_init().
 */
void pci_epc_mem_exit(struct pci_epc *epc)
{
	struct pci_epc_mem *mem = epc->mem;

	epc->mem = NULL;
	kfree(mem->bitmap);
	kfree(mem);
}
EXPORT_SYMBOL_GPL(pci_epc_mem_exit);

/**
 * pci_epc_mem_alloc_addr() - allocate memory address from EPC addr space
 * @epc: the EPC device on which memory has to be allocated
 * @phys_addr: populate the allocated physical address here
 * @size: the size of the address space that has to be allocated
 *
 * Invoke to allocate memory address from the EPC address space. This
 * is usually done to map the remote RC address into the local system.
 */
void __iomem *pci_epc_mem_alloc_addr(struct pci_epc *epc,
				     phys_addr_t *phys_addr, size_t size)
{
	int pageno;
	void __iomem *virt_addr;
	struct pci_epc_mem *mem = epc->mem;
	unsigned int page_shift = ilog2(mem->page_size);
	int order;

	size = ALIGN(size, mem->page_size);
	order = pci_epc_mem_get_order(mem, size);

	pageno = bitmap_find_free_region(mem->bitmap, mem->pages, order);
	if (pageno < 0)
		return NULL;

	*phys_addr = mem->phys_base + ((phys_addr_t)pageno << page_shift);
	virt_addr = ioremap(*phys_addr, size);
	if (!virt_addr)
		bitmap_release_region(mem->bitmap, pageno, order);

	return virt_addr;
}
EXPORT_SYMBOL_GPL(pci_epc_mem_alloc_addr);

/**
 * pci_epc_mem_free_addr() - free the allocated memory address
 * @epc: the EPC device on which memory was allocated
 * @phys_addr: the allocated physical address
 * @virt_addr: virtual address of the allocated mem space
 * @size: the size of the allocated address space
 *
 * Invoke to free the memory allocated using pci_epc_mem_alloc_addr.
 */
void pci_epc_mem_free_addr(struct pci_epc *epc, phys_addr_t phys_addr,
			   void __iomem *virt_addr, size_t size)
{
	int pageno;
	struct pci_epc_mem *mem = epc->mem;
	unsigned int page_shift = ilog2(mem->page_size);
	int order;

	iounmap(virt_addr);
	pageno = (phys_addr - mem->phys_base) >> page_shift;
	size = ALIGN(size, mem->page_size);
	order = pci_epc_mem_get_order(mem, size);
	bitmap_release_region(mem->bitmap, pageno, order);
}
EXPORT_SYMBOL_GPL(pci_epc_mem_free_addr);

MODULE_DESCRIPTION("PCI EPC Address Space Management");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");
