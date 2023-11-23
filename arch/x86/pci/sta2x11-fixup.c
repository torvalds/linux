// SPDX-License-Identifier: GPL-2.0-only
/*
 * DMA translation between STA2x11 AMBA memory mapping and the x86 memory mapping
 *
 * ST Microelectronics ConneXt (STA2X11/STA2X10)
 *
 * Copyright (c) 2010-2011 Wind River Systems, Inc.
 */

#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/dma-map-ops.h>
#include <linux/swiotlb.h>
#include <asm/iommu.h>
#include <asm/sta2x11.h>

#define STA2X11_SWIOTLB_SIZE (4*1024*1024)

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
		if (swiotlb_init_late(size, GFP_DMA, NULL))
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

/* This is exported, as some devices need to access the MFD registers */
struct sta2x11_instance *sta2x11_get_instance(struct pci_dev *pdev)
{
	return sta2x11_pdev_to_instance(pdev);
}
EXPORT_SYMBOL(sta2x11_get_instance);

/* At setup time, we use our own ops if the device is a ConneXt one */
static void sta2x11_setup_pdev(struct pci_dev *pdev)
{
	struct sta2x11_instance *instance = sta2x11_pdev_to_instance(pdev);

	if (!instance) /* either a sta2x11 bridge or another ST device */
		return;

	/* We must enable all devices as master, for audio DMA to work */
	pci_set_master(pdev);
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_STMICRO, PCI_ANY_ID, sta2x11_setup_pdev);

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
	struct sta2x11_instance *instance = sta2x11_pdev_to_instance(pdev);
	struct device *dev = &pdev->dev;
	u32 amba_base, max_amba_addr;
	int i, ret;

	if (!instance)
		return;

	pci_read_config_dword(pdev, AHB_BASE(0), &amba_base);
	max_amba_addr = amba_base + STA2X11_AMBA_SIZE - 1;

	ret = dma_direct_set_offset(dev, 0, amba_base, STA2X11_AMBA_SIZE);
	if (ret)
		dev_err(dev, "sta2x11: could not set DMA offset\n");

	dev->bus_dma_limit = max_amba_addr;
	dma_set_mask_and_coherent(&pdev->dev, max_amba_addr);

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
		 sta2x11_pdev_to_ep(pdev), amba_base, max_amba_addr);
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_STMICRO, PCI_ANY_ID, sta2x11_map_ep);

#ifdef CONFIG_PM /* Some register values must be saved and restored */

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
