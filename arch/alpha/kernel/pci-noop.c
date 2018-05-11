// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/pci-noop.c
 *
 * Stub PCI interfaces for Jensen-specific kernels.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/syscalls.h>

#include "proto.h"


/*
 * The PCI controller list.
 */

struct pci_controller *hose_head, **hose_tail = &hose_head;
struct pci_controller *pci_isa_hose;


struct pci_controller * __init
alloc_pci_controller(void)
{
	struct pci_controller *hose;

	hose = alloc_bootmem(sizeof(*hose));

	*hose_tail = hose;
	hose_tail = &hose->next;

	return hose;
}

struct resource * __init
alloc_resource(void)
{
	return alloc_bootmem(sizeof(struct resource));
}

SYSCALL_DEFINE3(pciconfig_iobase, long, which, unsigned long, bus,
		unsigned long, dfn)
{
	struct pci_controller *hose;

	/* from hose or from bus.devfn */
	if (which & IOBASE_FROM_HOSE) {
		for (hose = hose_head; hose; hose = hose->next) 
			if (hose->index == bus)
				break;
		if (!hose)
			return -ENODEV;
	} else {
		/* Special hook for ISA access.  */
		if (bus == 0 && dfn == 0)
			hose = pci_isa_hose;
		else
			return -ENODEV;
	}

	switch (which & ~IOBASE_FROM_HOSE) {
	case IOBASE_HOSE:
		return hose->index;
	case IOBASE_SPARSE_MEM:
		return hose->sparse_mem_base;
	case IOBASE_DENSE_MEM:
		return hose->dense_mem_base;
	case IOBASE_SPARSE_IO:
		return hose->sparse_io_base;
	case IOBASE_DENSE_IO:
		return hose->dense_io_base;
	case IOBASE_ROOT_BUS:
		return hose->bus->number;
	}

	return -EOPNOTSUPP;
}

SYSCALL_DEFINE5(pciconfig_read, unsigned long, bus, unsigned long, dfn,
		unsigned long, off, unsigned long, len, void __user *, buf)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	else
		return -ENODEV;
}

SYSCALL_DEFINE5(pciconfig_write, unsigned long, bus, unsigned long, dfn,
		unsigned long, off, unsigned long, len, void __user *, buf)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	else
		return -ENODEV;
}

static void *alpha_noop_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t gfp,
				       unsigned long attrs)
{
	void *ret;

	if (!dev || *dev->dma_mask >= 0xffffffffUL)
		gfp &= ~GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));
	if (ret) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}
	return ret;
}

static int alpha_noop_supported(struct device *dev, u64 mask)
{
	return mask < 0x00ffffffUL ? 0 : 1;
}

const struct dma_map_ops alpha_noop_ops = {
	.alloc			= alpha_noop_alloc_coherent,
	.free			= dma_noop_free_coherent,
	.map_page		= dma_noop_map_page,
	.map_sg			= dma_noop_map_sg,
	.mapping_error		= dma_noop_mapping_error,
	.dma_supported		= alpha_noop_supported,
};

const struct dma_map_ops *dma_ops = &alpha_noop_ops;
EXPORT_SYMBOL(dma_ops);
