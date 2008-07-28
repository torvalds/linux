/*
 * HP Quicksilver AGP GART routines
 *
 * Copyright (c) 2006, Kyle McMartin <kyle@parisc-linux.org>
 *
 * Based on drivers/char/agpgart/hp-agp.c which is
 * (c) Copyright 2002, 2003 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/klist.h>
#include <linux/agp_backend.h>
#include <linux/log2.h>

#include <asm/parisc-device.h>
#include <asm/ropes.h>

#include "agp.h"

#define DRVNAME	"quicksilver"
#define DRVPFX	DRVNAME ": "

#define AGP8X_MODE_BIT		3
#define AGP8X_MODE		(1 << AGP8X_MODE_BIT)

static struct _parisc_agp_info {
	void __iomem *ioc_regs;
	void __iomem *lba_regs;

	int lba_cap_offset;

	u64 *gatt;
	u64 gatt_entries;

	u64 gart_base;
	u64 gart_size;

	int io_page_size;
	int io_pages_per_kpage;
} parisc_agp_info;

static struct gatt_mask parisc_agp_masks[] =
{
        {
		.mask = SBA_PDIR_VALID_BIT,
		.type = 0
	}
};

static struct aper_size_info_fixed parisc_agp_sizes[] =
{
        {0, 0, 0},              /* filled in by parisc_agp_fetch_size() */
};

static int
parisc_agp_fetch_size(void)
{
	int size;

	size = parisc_agp_info.gart_size / MB(1);
	parisc_agp_sizes[0].size = size;
	agp_bridge->current_size = (void *) &parisc_agp_sizes[0];

	return size;
}

static int
parisc_agp_configure(void)
{
	struct _parisc_agp_info *info = &parisc_agp_info;

	agp_bridge->gart_bus_addr = info->gart_base;
	agp_bridge->capndx = info->lba_cap_offset;
	agp_bridge->mode = readl(info->lba_regs+info->lba_cap_offset+PCI_AGP_STATUS);

	return 0;
}

static void
parisc_agp_tlbflush(struct agp_memory *mem)
{
	struct _parisc_agp_info *info = &parisc_agp_info;

	writeq(info->gart_base | ilog2(info->gart_size), info->ioc_regs+IOC_PCOM);
	readq(info->ioc_regs+IOC_PCOM);	/* flush */
}

static int
parisc_agp_create_gatt_table(struct agp_bridge_data *bridge)
{
	struct _parisc_agp_info *info = &parisc_agp_info;
	int i;

	for (i = 0; i < info->gatt_entries; i++) {
		info->gatt[i] = (unsigned long)agp_bridge->scratch_page;
	}

	return 0;
}

static int
parisc_agp_free_gatt_table(struct agp_bridge_data *bridge)
{
	struct _parisc_agp_info *info = &parisc_agp_info;

	info->gatt[0] = SBA_AGPGART_COOKIE;

	return 0;
}

static int
parisc_agp_insert_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	struct _parisc_agp_info *info = &parisc_agp_info;
	int i, k;
	off_t j, io_pg_start;
	int io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = info->io_pages_per_kpage * pg_start;
	io_pg_count = info->io_pages_per_kpage * mem->page_count;
	if ((io_pg_start + io_pg_count) > info->gatt_entries) {
		return -EINVAL;
	}

	j = io_pg_start;
	while (j < (io_pg_start + io_pg_count)) {
		if (info->gatt[j])
			return -EBUSY;
		j++;
	}

	if (!mem->is_flushed) {
		global_cache_flush();
		mem->is_flushed = true;
	}

	for (i = 0, j = io_pg_start; i < mem->page_count; i++) {
		unsigned long paddr;

		paddr = mem->memory[i];
		for (k = 0;
		     k < info->io_pages_per_kpage;
		     k++, j++, paddr += info->io_page_size) {
			info->gatt[j] =
				agp_bridge->driver->mask_memory(agp_bridge,
					paddr, type);
		}
	}

	agp_bridge->driver->tlb_flush(mem);

	return 0;
}

static int
parisc_agp_remove_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	struct _parisc_agp_info *info = &parisc_agp_info;
	int i, io_pg_start, io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = info->io_pages_per_kpage * pg_start;
	io_pg_count = info->io_pages_per_kpage * mem->page_count;
	for (i = io_pg_start; i < io_pg_count + io_pg_start; i++) {
		info->gatt[i] = agp_bridge->scratch_page;
	}

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static unsigned long
parisc_agp_mask_memory(struct agp_bridge_data *bridge,
		    unsigned long addr, int type)
{
	return SBA_PDIR_VALID_BIT | addr;
}

static void
parisc_agp_enable(struct agp_bridge_data *bridge, u32 mode)
{
	struct _parisc_agp_info *info = &parisc_agp_info;
	u32 command;

	command = readl(info->lba_regs + info->lba_cap_offset + PCI_AGP_STATUS);

	command = agp_collect_device_status(bridge, mode, command);
	command |= 0x00000100;

	writel(command, info->lba_regs + info->lba_cap_offset + PCI_AGP_COMMAND);

	agp_device_command(command, (mode & AGP8X_MODE) != 0);
}

static const struct agp_bridge_driver parisc_agp_driver = {
	.owner			= THIS_MODULE,
	.size_type		= FIXED_APER_SIZE,
	.configure		= parisc_agp_configure,
	.fetch_size		= parisc_agp_fetch_size,
	.tlb_flush		= parisc_agp_tlbflush,
	.mask_memory		= parisc_agp_mask_memory,
	.masks			= parisc_agp_masks,
	.agp_enable		= parisc_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= parisc_agp_create_gatt_table,
	.free_gatt_table	= parisc_agp_free_gatt_table,
	.insert_memory		= parisc_agp_insert_memory,
	.remove_memory		= parisc_agp_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
	.cant_use_aperture	= true,
};

static int __init
agp_ioc_init(void __iomem *ioc_regs)
{
	struct _parisc_agp_info *info = &parisc_agp_info;
        u64 iova_base, *io_pdir, io_tlb_ps;
        int io_tlb_shift;

        printk(KERN_INFO DRVPFX "IO PDIR shared with sba_iommu\n");

        info->ioc_regs = ioc_regs;

        io_tlb_ps = readq(info->ioc_regs+IOC_TCNFG);
        switch (io_tlb_ps) {
        case 0: io_tlb_shift = 12; break;
        case 1: io_tlb_shift = 13; break;
        case 2: io_tlb_shift = 14; break;
        case 3: io_tlb_shift = 16; break;
        default:
                printk(KERN_ERR DRVPFX "Invalid IOTLB page size "
                       "configuration 0x%llx\n", io_tlb_ps);
                info->gatt = NULL;
                info->gatt_entries = 0;
                return -ENODEV;
        }
        info->io_page_size = 1 << io_tlb_shift;
        info->io_pages_per_kpage = PAGE_SIZE / info->io_page_size;

        iova_base = readq(info->ioc_regs+IOC_IBASE) & ~0x1;
        info->gart_base = iova_base + PLUTO_IOVA_SIZE - PLUTO_GART_SIZE;

        info->gart_size = PLUTO_GART_SIZE;
        info->gatt_entries = info->gart_size / info->io_page_size;

        io_pdir = phys_to_virt(readq(info->ioc_regs+IOC_PDIR_BASE));
        info->gatt = &io_pdir[(PLUTO_IOVA_SIZE/2) >> PAGE_SHIFT];

        if (info->gatt[0] != SBA_AGPGART_COOKIE) {
                info->gatt = NULL;
                info->gatt_entries = 0;
                printk(KERN_ERR DRVPFX "No reserved IO PDIR entry found; "
                       "GART disabled\n");
                return -ENODEV;
        }

        return 0;
}

static int
lba_find_capability(int cap)
{
	struct _parisc_agp_info *info = &parisc_agp_info;
        u16 status;
        u8 pos, id;
        int ttl = 48;

        status = readw(info->lba_regs + PCI_STATUS);
        if (!(status & PCI_STATUS_CAP_LIST))
                return 0;
        pos = readb(info->lba_regs + PCI_CAPABILITY_LIST);
        while (ttl-- && pos >= 0x40) {
                pos &= ~3;
                id = readb(info->lba_regs + pos + PCI_CAP_LIST_ID);
                if (id == 0xff)
                        break;
                if (id == cap)
                        return pos;
                pos = readb(info->lba_regs + pos + PCI_CAP_LIST_NEXT);
        }
        return 0;
}

static int __init
agp_lba_init(void __iomem *lba_hpa)
{
	struct _parisc_agp_info *info = &parisc_agp_info;
        int cap;

	info->lba_regs = lba_hpa;
        info->lba_cap_offset = lba_find_capability(PCI_CAP_ID_AGP);

        cap = readl(lba_hpa + info->lba_cap_offset) & 0xff;
        if (cap != PCI_CAP_ID_AGP) {
                printk(KERN_ERR DRVPFX "Invalid capability ID 0x%02x at 0x%x\n",
                       cap, info->lba_cap_offset);
                return -ENODEV;
        }

        return 0;
}

static int __init
parisc_agp_setup(void __iomem *ioc_hpa, void __iomem *lba_hpa)
{
	struct pci_dev *fake_bridge_dev = NULL;
	struct agp_bridge_data *bridge;
	int error = 0;

	fake_bridge_dev = alloc_pci_dev();
	if (!fake_bridge_dev) {
		error = -ENOMEM;
		goto fail;
	}

	error = agp_ioc_init(ioc_hpa);
	if (error)
		goto fail;

	error = agp_lba_init(lba_hpa);
	if (error)
		goto fail;

	bridge = agp_alloc_bridge();
	if (!bridge) {
		error = -ENOMEM;
		goto fail;
	}
	bridge->driver = &parisc_agp_driver;

	fake_bridge_dev->vendor = PCI_VENDOR_ID_HP;
	fake_bridge_dev->device = PCI_DEVICE_ID_HP_PCIX_LBA;
	bridge->dev = fake_bridge_dev;

	error = agp_add_bridge(bridge);

fail:
	return error;
}

static struct device *next_device(struct klist_iter *i) {
	struct klist_node * n = klist_next(i);
	return n ? container_of(n, struct device, knode_parent) : NULL;
}

static int
parisc_agp_init(void)
{
	extern struct sba_device *sba_list;

	int err = -1;
	struct parisc_device *sba = NULL, *lba = NULL;
	struct lba_device *lbadev = NULL;
	struct device *dev = NULL;
	struct klist_iter i;

	if (!sba_list)
		goto out;

	/* Find our parent Pluto */
	sba = sba_list->dev;
	if (!IS_PLUTO(sba)) {
		printk(KERN_INFO DRVPFX "No Pluto found, so no AGPGART for you.\n");
		goto out;
	}

	/* Now search our Pluto for our precious AGP device... */
	klist_iter_init(&sba->dev.klist_children, &i);
	while ((dev = next_device(&i))) {
		struct parisc_device *padev = to_parisc_device(dev);
		if (IS_QUICKSILVER(padev))
			lba = padev;
	}
	klist_iter_exit(&i);

	if (!lba) {
		printk(KERN_INFO DRVPFX "No AGP devices found.\n");
		goto out;
	}

	lbadev = parisc_get_drvdata(lba);

	/* w00t, let's go find our cookies... */
	parisc_agp_setup(sba_list->ioc[0].ioc_hpa, lbadev->hba.base_addr);

	return 0;

out:
	return err;
}

module_init(parisc_agp_init);

MODULE_AUTHOR("Kyle McMartin <kyle@parisc-linux.org>");
MODULE_LICENSE("GPL");
