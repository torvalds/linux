/*
 * arch/x86/pci/sta2x11-fixup.c
 * glue code for lib/swiotlb.c and DMA translation between STA2x11
 * AMBA memory mapping and the X86 memory mapping
 *
 * ST Microelectronics ConneXt (STA2X11/STA2X10)
 *
 * Copyright (c) 2010-2011 Wind River Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/export.h>
#include <linux/list.h>
#include <asm/iommu.h>

#define STA2X11_SWIOTLB_SIZE (4*1024*1024)
extern int swiotlb_late_init_with_default_size(size_t default_size);

/*
 * We build a list of bus numbers that are under the ConneXt. The
 * main bridge hosts 4 busses, which are the 4 endpoints, in order.
 */
#define STA2X11_NR_EP		4	/* 0..3 included */
#define STA2X11_NR_FUNCS	8	/* 0..7 included */
#define STA2X11_AMBA_SIZE	(512 << 20)

struct sta2x11_ahb_regs { /* saved during suspend */
	u32 base, pexlbase, pexhbase, crw;
};

struct sta2x11_mapping {
	u32 amba_base;
	int is_suspended;
	struct sta2x11_ahb_regs regs[STA2X11_NR_FUNCS];
};

struct sta2x11_instance {
	struct list_head list;
	int bus0;
	struct sta2x11_mapping map[STA2X11_NR_EP];
};

static LIST_HEAD(sta2x11_instance_list);

/* At probe time, record new instances of this bridge (likely one only) */
static void sta2x11_new_instance(struct pci_dev *pdev)
{
	struct sta2x11_instance *instance;

	instance = kzalloc(sizeof(*instance), GFP_ATOMIC);
	if (!instance)
		return;
	/* This has a subordinate bridge, with 4 more-subordinate ones */
	instance->bus0 = pdev->subordinate->number + 1;

	if (list_empty(&sta2x11_instance_list)) {
		int size = STA2X11_SWIOTLB_SIZE;
		/* First instance: register your own swiotlb area */
		dev_info(&pdev->dev, "Using SWIOTLB (size %i)\n", size);
		if (swiotlb_late_init_with_default_size(size))
			dev_emerg(&pdev->dev, "init swiotlb failed\n");
	}
	list_add(&instance->list, &sta2x11_instance_list);
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_STMICRO, 0xcc17, sta2x11_new_instance);

/*
 * Utility functions used in this file from below
 */
static struct sta2x11_instance *sta2x11_pdev_to_instance(struct pci_dev *pdev)
{
	struct sta2x11_instance *instance;
	int ep;

	list_for_each_entry(instance, &sta2x11_instance_list, list) {
		ep = pdev->bus->number - instance->bus0;
		if (ep >= 0 && ep < STA2X11_NR_EP)
			return instance;
	}
	return NULL;
}

static int sta2x11_pdev_to_ep(struct pci_dev *pdev)
{
	struct sta2x11_instance *instance;

	instance = sta2x11_pdev_to_instance(pdev);
	if (!instance)
		return -1;

	return pdev->bus->number - instance->bus0;
}

static struct sta2x11_mapping *sta2x11_pdev_to_mapping(struct pci_dev *pdev)
{
	struct sta2x11_instance *instance;
	int ep;

	instance = sta2x11_pdev_to_instance(pdev);
	if (!instance)
		return NULL;
	ep = sta2x11_pdev_to_ep(pdev);
	return instance->map + ep;
}

/* This is exported, as some devices need to access the MFD registers */
struct sta2x11_instance *sta2x11_get_instance(struct pci_dev *pdev)
{
	return sta2x11_pdev_to_instance(pdev);
}
EXPORT_SYMBOL(sta2x11_get_instance);


/**
 * p2a - Translate physical address to STA2x11 AMBA address,
 *       used for DMA transfers to STA2x11
 * @p: Physical address
 * @pdev: PCI device (must be hosted within the connext)
 */
static dma_addr_t p2a(dma_addr_t p, struct pci_dev *pdev)
{
	struct sta2x11_mapping *map;
	dma_addr_t a;

	map = sta2x11_pdev_to_mapping(pdev);
	a = p + map->amba_base;
	return a;
}

/**
 * a2p - Translate STA2x11 AMBA address to physical address
 *       used for DMA transfers from STA2x11
 * @a: STA2x11 AMBA address
 * @pdev: PCI device (must be hosted within the connext)
 */
static dma_addr_t a2p(dma_addr_t a, struct pci_dev *pdev)
{
	struct sta2x11_mapping *map;
	dma_addr_t p;

	map = sta2x11_pdev_to_mapping(pdev);
	p = a - map->amba_base;
	return p;
}

/**
 * sta2x11_swiotlb_alloc_coherent - Allocate swiotlb bounce buffers
 *     returns virtual address. This is the only "special" function here.
 * @dev: PCI device
 * @size: Size of the buffer
 * @dma_handle: DMA address
 * @flags: memory flags
 */
static void *sta2x11_swiotlb_alloc_coherent(struct device *dev,
					    size_t size,
					    dma_addr_t *dma_handle,
					    gfp_t flags,
					    unsigned long attrs)
{
	void *vaddr;

	vaddr = x86_swiotlb_alloc_coherent(dev, size, dma_handle, flags, attrs);
	*dma_handle = p2a(*dma_handle, to_pci_dev(dev));
	return vaddr;
}

/* We have our own dma_ops: the same as swiotlb but from alloc (above) */
static const struct dma_map_ops sta2x11_dma_ops = {
	.alloc = sta2x11_swiotlb_alloc_coherent,
	.free = x86_swiotlb_free_coherent,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.mapping_error = swiotlb_dma_mapping_error,
	.dma_supported = x86_dma_supported,
};

/* At setup time, we use our own ops if the device is a ConneXt one */
static void sta2x11_setup_pdev(struct pci_dev *pdev)
{
	struct sta2x11_instance *instance = sta2x11_pdev_to_instance(pdev);

	if (!instance) /* either a sta2x11 bridge or another ST device */
		return;
	pci_set_consistent_dma_mask(pdev, STA2X11_AMBA_SIZE - 1);
	pci_set_dma_mask(pdev, STA2X11_AMBA_SIZE - 1);
	pdev->dev.dma_ops = &sta2x11_dma_ops;

	/* We must enable all devices as master, for audio DMA to work */
	pci_set_master(pdev);
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_STMICRO, PCI_ANY_ID, sta2x11_setup_pdev);

/*
 * The following three functions are exported (used in swiotlb: FIXME)
 */
/**
 * dma_capable - Check if device can manage DMA transfers (FIXME: kill it)
 * @dev: device for a PCI device
 * @addr: DMA address
 * @size: DMA size
 */
bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	struct sta2x11_mapping *map;

	if (dev->dma_ops != &sta2x11_dma_ops) {
		if (!dev->dma_mask)
			return false;
		return addr + size - 1 <= *dev->dma_mask;
	}

	map = sta2x11_pdev_to_mapping(to_pci_dev(dev));

	if (!map || (addr < map->amba_base))
		return false;
	if (addr + size >= map->amba_base + STA2X11_AMBA_SIZE) {
		return false;
	}

	return true;
}

/**
 * phys_to_dma - Return the DMA AMBA address used for this STA2x11 device
 * @dev: device for a PCI device
 * @paddr: Physical address
 */
dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	if (dev->dma_ops != &sta2x11_dma_ops)
		return paddr;
	return p2a(paddr, to_pci_dev(dev));
}

/**
 * dma_to_phys - Return the physical address used for this STA2x11 DMA address
 * @dev: device for a PCI device
 * @daddr: STA2x11 AMBA DMA address
 */
phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	if (dev->dma_ops != &sta2x11_dma_ops)
		return daddr;
	return a2p(daddr, to_pci_dev(dev));
}


/*
 * At boot we must set up the mappings for the pcie-to-amba bridge.
 * It involves device access, and the same happens at suspend/resume time
 */

#define AHB_MAPB		0xCA4
#define AHB_CRW(i)		(AHB_MAPB + 0  + (i) * 0x10)
#define AHB_CRW_SZMASK			0xfffffc00UL
#define AHB_CRW_ENABLE			(1 << 0)
#define AHB_CRW_WTYPE_MEM		(2 << 1)
#define AHB_CRW_ROE			(1UL << 3)	/* Relax Order Ena */
#define AHB_CRW_NSE			(1UL << 4)	/* No Snoop Enable */
#define AHB_BASE(i)		(AHB_MAPB + 4  + (i) * 0x10)
#define AHB_PEXLBASE(i)		(AHB_MAPB + 8  + (i) * 0x10)
#define AHB_PEXHBASE(i)		(AHB_MAPB + 12 + (i) * 0x10)

/* At probe time, enable mapping for each endpoint, using the pdev */
static void sta2x11_map_ep(struct pci_dev *pdev)
{
	struct sta2x11_mapping *map = sta2x11_pdev_to_mapping(pdev);
	int i;

	if (!map)
		return;
	pci_read_config_dword(pdev, AHB_BASE(0), &map->amba_base);

	/* Configure AHB mapping */
	pci_write_config_dword(pdev, AHB_PEXLBASE(0), 0);
	pci_write_config_dword(pdev, AHB_PEXHBASE(0), 0);
	pci_write_config_dword(pdev, AHB_CRW(0), STA2X11_AMBA_SIZE |
			       AHB_CRW_WTYPE_MEM | AHB_CRW_ENABLE);

	/* Disable all the other windows */
	for (i = 1; i < STA2X11_NR_FUNCS; i++)
		pci_write_config_dword(pdev, AHB_CRW(i), 0);

	dev_info(&pdev->dev,
		 "sta2x11: Map EP %i: AMBA address %#8x-%#8x\n",
		 sta2x11_pdev_to_ep(pdev),  map->amba_base,
		 map->amba_base + STA2X11_AMBA_SIZE - 1);
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_STMICRO, PCI_ANY_ID, sta2x11_map_ep);

#ifdef CONFIG_PM /* Some register values must be saved and restored */

static void suspend_mapping(struct pci_dev *pdev)
{
	struct sta2x11_mapping *map = sta2x11_pdev_to_mapping(pdev);
	int i;

	if (!map)
		return;

	if (map->is_suspended)
		return;
	map->is_suspended = 1;

	/* Save all window configs */
	for (i = 0; i < STA2X11_NR_FUNCS; i++) {
		struct sta2x11_ahb_regs *regs = map->regs + i;

		pci_read_config_dword(pdev, AHB_BASE(i), &regs->base);
		pci_read_config_dword(pdev, AHB_PEXLBASE(i), &regs->pexlbase);
		pci_read_config_dword(pdev, AHB_PEXHBASE(i), &regs->pexhbase);
		pci_read_config_dword(pdev, AHB_CRW(i), &regs->crw);
	}
}
DECLARE_PCI_FIXUP_SUSPEND(PCI_VENDOR_ID_STMICRO, PCI_ANY_ID, suspend_mapping);

static void resume_mapping(struct pci_dev *pdev)
{
	struct sta2x11_mapping *map = sta2x11_pdev_to_mapping(pdev);
	int i;

	if (!map)
		return;


	if (!map->is_suspended)
		goto out;
	map->is_suspended = 0;

	/* Restore all window configs */
	for (i = 0; i < STA2X11_NR_FUNCS; i++) {
		struct sta2x11_ahb_regs *regs = map->regs + i;

		pci_write_config_dword(pdev, AHB_BASE(i), regs->base);
		pci_write_config_dword(pdev, AHB_PEXLBASE(i), regs->pexlbase);
		pci_write_config_dword(pdev, AHB_PEXHBASE(i), regs->pexhbase);
		pci_write_config_dword(pdev, AHB_CRW(i), regs->crw);
	}
out:
	pci_set_master(pdev); /* Like at boot, enable master on all devices */
}
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_STMICRO, PCI_ANY_ID, resume_mapping);

#endif /* CONFIG_PM */
