// SPDX-License-Identifier: GPL-2.0
/*
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
	unsigned int page_shift = ilog2(mem->window.page_size);

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
 * pci_epc_multi_mem_init() - initialize the pci_epc_mem structure
 * @epc: the EPC device that invoked pci_epc_mem_init
 * @windows: pointer to windows supported by the device
 * @num_windows: number of windows device supports
 *
 * Invoke to initialize the pci_epc_mem structure used by the
 * endpoint functions to allocate mapped PCI address.
 */
int pci_epc_multi_mem_init(struct pci_epc *epc,
			   struct pci_epc_mem_window *windows,
			   unsigned int num_windows)
{
	struct pci_epc_mem *mem = NULL;
	unsigned long *bitmap = NULL;
	unsigned int page_shift;
	size_t page_size;
	int bitmap_size;
	int pages;
	int ret;
	int i;

	epc->num_windows = 0;

	if (!windows || !num_windows)
		return -EINVAL;

	epc->windows = kcalloc(num_windows, sizeof(*epc->windows), GFP_KERNEL);
	if (!epc->windows)
		return -ENOMEM;

	for (i = 0; i < num_windows; i++) {
		page_size = windows[i].page_size;
		if (page_size < PAGE_SIZE)
			page_size = PAGE_SIZE;
		page_shift = ilog2(page_size);
		pages = windows[i].size >> page_shift;
		bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

		mem = kzalloc(sizeof(*mem), GFP_KERNEL);
		if (!mem) {
			ret = -ENOMEM;
			i--;
			goto err_mem;
		}

		bitmap = kzalloc(bitmap_size, GFP_KERNEL);
		if (!bitmap) {
			ret = -ENOMEM;
			kfree(mem);
			i--;
			goto err_mem;
		}

		mem->window.phys_base = windows[i].phys_base;
		mem->window.size = windows[i].size;
		mem->window.page_size = page_size;
		mem->bitmap = bitmap;
		mem->pages = pages;
		mutex_init(&mem->lock);
		epc->windows[i] = mem;
	}

	epc->mem = epc->windows[0];
	epc->num_windows = num_windows;

	return 0;

err_mem:
	for (; i >= 0; i--) {
		mem = epc->windows[i];
		kfree(mem->bitmap);
		kfree(mem);
	}
	kfree(epc->windows);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_multi_mem_init);

/**
 * pci_epc_mem_init() - Initialize the pci_epc_mem structure
 * @epc: the EPC device that invoked pci_epc_mem_init
 * @base: Physical address of the window region
 * @size: Total Size of the window region
 * @page_size: Page size of the window region
 *
 * Invoke to initialize a single pci_epc_mem structure used by the
 * endpoint functions to allocate memory for mapping the PCI host memory
 */
int pci_epc_mem_init(struct pci_epc *epc, phys_addr_t base,
		     size_t size, size_t page_size)
{
	struct pci_epc_mem_window mem_window;

	mem_window.phys_base = base;
	mem_window.size = size;
	mem_window.page_size = page_size;

	return pci_epc_multi_mem_init(epc, &mem_window, 1);
}
EXPORT_SYMBOL_GPL(pci_epc_mem_init);

/**
 * pci_epc_mem_exit() - cleanup the pci_epc_mem structure
 * @epc: the EPC device that invoked pci_epc_mem_exit
 *
 * Invoke to cleanup the pci_epc_mem structure allocated in
 * pci_epc_mem_init().
 */
void pci_epc_mem_exit(struct pci_epc *epc)
{
	struct pci_epc_mem *mem;
	int i;

	if (!epc->num_windows)
		return;

	for (i = 0; i < epc->num_windows; i++) {
		mem = epc->windows[i];
		kfree(mem->bitmap);
		kfree(mem);
	}
	kfree(epc->windows);

	epc->windows = NULL;
	epc->mem = NULL;
	epc->num_windows = 0;
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
	void __iomem *virt_addr;
	struct pci_epc_mem *mem;
	unsigned int page_shift;
	size_t align_size;
	int pageno;
	int order;
	int i;

	for (i = 0; i < epc->num_windows; i++) {
		mem = epc->windows[i];
		if (size > mem->window.size)
			continue;

		align_size = ALIGN(size, mem->window.page_size);
		order = pci_epc_mem_get_order(mem, align_size);

		mutex_lock(&mem->lock);
		pageno = bitmap_find_free_region(mem->bitmap, mem->pages,
						 order);
		if (pageno >= 0) {
			page_shift = ilog2(mem->window.page_size);
			*phys_addr = mem->window.phys_base +
				((phys_addr_t)pageno << page_shift);
			virt_addr = ioremap(*phys_addr, align_size);
			if (!virt_addr) {
				bitmap_release_region(mem->bitmap,
						      pageno, order);
				mutex_unlock(&mem->lock);
				continue;
			}
			mutex_unlock(&mem->lock);
			return virt_addr;
		}
		mutex_unlock(&mem->lock);
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(pci_epc_mem_alloc_addr);

static struct pci_epc_mem *pci_epc_get_matching_window(struct pci_epc *epc,
						       phys_addr_t phys_addr)
{
	struct pci_epc_mem *mem;
	int i;

	for (i = 0; i < epc->num_windows; i++) {
		mem = epc->windows[i];

		if (phys_addr >= mem->window.phys_base &&
		    phys_addr < (mem->window.phys_base + mem->window.size))
			return mem;
	}

	return NULL;
}

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
	struct pci_epc_mem *mem;
	unsigned int page_shift;
	size_t page_size;
	int pageno;
	int order;

	mem = pci_epc_get_matching_window(epc, phys_addr);
	if (!mem) {
		pr_err("failed to get matching window\n");
		return;
	}

	page_size = mem->window.page_size;
	page_shift = ilog2(page_size);
	iounmap(virt_addr);
	pageno = (phys_addr - mem->window.phys_base) >> page_shift;
	size = ALIGN(size, page_size);
	order = pci_epc_mem_get_order(mem, size);
	mutex_lock(&mem->lock);
	bitmap_release_region(mem->bitmap, pageno, order);
	mutex_unlock(&mem->lock);
}
EXPORT_SYMBOL_GPL(pci_epc_mem_free_addr);

MODULE_DESCRIPTION("PCI EPC Address Space Management");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
