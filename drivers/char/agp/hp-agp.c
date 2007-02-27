/*
 * HP zx1 AGPGART routines.
 *
 * (c) Copyright 2002, 2003 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>

#include <asm/acpi-ext.h>

#include "agp.h"

#ifndef log2
#define log2(x)		ffz(~(x))
#endif

#define HP_ZX1_IOC_OFFSET	0x1000  /* ACPI reports SBA, we want IOC */

/* HP ZX1 IOC registers */
#define HP_ZX1_IBASE		0x300
#define HP_ZX1_IMASK		0x308
#define HP_ZX1_PCOM		0x310
#define HP_ZX1_TCNFG		0x318
#define HP_ZX1_PDIR_BASE	0x320

#define HP_ZX1_IOVA_BASE	GB(1UL)
#define HP_ZX1_IOVA_SIZE	GB(1UL)
#define HP_ZX1_GART_SIZE	(HP_ZX1_IOVA_SIZE / 2)
#define HP_ZX1_SBA_IOMMU_COOKIE	0x0000badbadc0ffeeUL

#define HP_ZX1_PDIR_VALID_BIT	0x8000000000000000UL
#define HP_ZX1_IOVA_TO_PDIR(va)	((va - hp_private.iova_base) >> hp_private.io_tlb_shift)

#define AGP8X_MODE_BIT		3
#define AGP8X_MODE		(1 << AGP8X_MODE_BIT)

/* AGP bridge need not be PCI device, but DRM thinks it is. */
static struct pci_dev fake_bridge_dev;

static int hp_zx1_gart_found;

static struct aper_size_info_fixed hp_zx1_sizes[] =
{
	{0, 0, 0},		/* filled in by hp_zx1_fetch_size() */
};

static struct gatt_mask hp_zx1_masks[] =
{
	{.mask = HP_ZX1_PDIR_VALID_BIT, .type = 0}
};

static struct _hp_private {
	volatile u8 __iomem *ioc_regs;
	volatile u8 __iomem *lba_regs;
	int lba_cap_offset;
	u64 *io_pdir;		// PDIR for entire IOVA
	u64 *gatt;		// PDIR just for GART (subset of above)
	u64 gatt_entries;
	u64 iova_base;
	u64 gart_base;
	u64 gart_size;
	u64 io_pdir_size;
	int io_pdir_owner;	// do we own it, or share it with sba_iommu?
	int io_page_size;
	int io_tlb_shift;
	int io_tlb_ps;		// IOC ps config
	int io_pages_per_kpage;
} hp_private;

static int __init hp_zx1_ioc_shared(void)
{
	struct _hp_private *hp = &hp_private;

	printk(KERN_INFO PFX "HP ZX1 IOC: IOPDIR shared with sba_iommu\n");

	/*
	 * IOC already configured by sba_iommu module; just use
	 * its setup.  We assume:
	 *	- IOVA space is 1Gb in size
	 *	- first 512Mb is IOMMU, second 512Mb is GART
	 */
	hp->io_tlb_ps = readq(hp->ioc_regs+HP_ZX1_TCNFG);
	switch (hp->io_tlb_ps) {
		case 0: hp->io_tlb_shift = 12; break;
		case 1: hp->io_tlb_shift = 13; break;
		case 2: hp->io_tlb_shift = 14; break;
		case 3: hp->io_tlb_shift = 16; break;
		default:
			printk(KERN_ERR PFX "Invalid IOTLB page size "
			       "configuration 0x%x\n", hp->io_tlb_ps);
			hp->gatt = NULL;
			hp->gatt_entries = 0;
			return -ENODEV;
	}
	hp->io_page_size = 1 << hp->io_tlb_shift;
	hp->io_pages_per_kpage = PAGE_SIZE / hp->io_page_size;

	hp->iova_base = readq(hp->ioc_regs+HP_ZX1_IBASE) & ~0x1;
	hp->gart_base = hp->iova_base + HP_ZX1_IOVA_SIZE - HP_ZX1_GART_SIZE;

	hp->gart_size = HP_ZX1_GART_SIZE;
	hp->gatt_entries = hp->gart_size / hp->io_page_size;

	hp->io_pdir = gart_to_virt(readq(hp->ioc_regs+HP_ZX1_PDIR_BASE));
	hp->gatt = &hp->io_pdir[HP_ZX1_IOVA_TO_PDIR(hp->gart_base)];

	if (hp->gatt[0] != HP_ZX1_SBA_IOMMU_COOKIE) {
		/* Normal case when no AGP device in system */
		hp->gatt = NULL;
		hp->gatt_entries = 0;
		printk(KERN_ERR PFX "No reserved IO PDIR entry found; "
		       "GART disabled\n");
		return -ENODEV;
	}

	return 0;
}

static int __init
hp_zx1_ioc_owner (void)
{
	struct _hp_private *hp = &hp_private;

	printk(KERN_INFO PFX "HP ZX1 IOC: IOPDIR dedicated to GART\n");

	/*
	 * Select an IOV page size no larger than system page size.
	 */
	if (PAGE_SIZE >= KB(64)) {
		hp->io_tlb_shift = 16;
		hp->io_tlb_ps = 3;
	} else if (PAGE_SIZE >= KB(16)) {
		hp->io_tlb_shift = 14;
		hp->io_tlb_ps = 2;
	} else if (PAGE_SIZE >= KB(8)) {
		hp->io_tlb_shift = 13;
		hp->io_tlb_ps = 1;
	} else {
		hp->io_tlb_shift = 12;
		hp->io_tlb_ps = 0;
	}
	hp->io_page_size = 1 << hp->io_tlb_shift;
	hp->io_pages_per_kpage = PAGE_SIZE / hp->io_page_size;

	hp->iova_base = HP_ZX1_IOVA_BASE;
	hp->gart_size = HP_ZX1_GART_SIZE;
	hp->gart_base = hp->iova_base + HP_ZX1_IOVA_SIZE - hp->gart_size;

	hp->gatt_entries = hp->gart_size / hp->io_page_size;
	hp->io_pdir_size = (HP_ZX1_IOVA_SIZE / hp->io_page_size) * sizeof(u64);

	return 0;
}

static int __init
hp_zx1_ioc_init (u64 hpa)
{
	struct _hp_private *hp = &hp_private;

	hp->ioc_regs = ioremap(hpa, 1024);
	if (!hp->ioc_regs)
		return -ENOMEM;

	/*
	 * If the IOTLB is currently disabled, we can take it over.
	 * Otherwise, we have to share with sba_iommu.
	 */
	hp->io_pdir_owner = (readq(hp->ioc_regs+HP_ZX1_IBASE) & 0x1) == 0;

	if (hp->io_pdir_owner)
		return hp_zx1_ioc_owner();

	return hp_zx1_ioc_shared();
}

static int
hp_zx1_lba_find_capability (volatile u8 __iomem *hpa, int cap)
{
	u16 status;
	u8 pos, id;
	int ttl = 48;

	status = readw(hpa+PCI_STATUS);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;
	pos = readb(hpa+PCI_CAPABILITY_LIST);
	while (ttl-- && pos >= 0x40) {
		pos &= ~3;
		id = readb(hpa+pos+PCI_CAP_LIST_ID);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = readb(hpa+pos+PCI_CAP_LIST_NEXT);
	}
	return 0;
}

static int __init
hp_zx1_lba_init (u64 hpa)
{
	struct _hp_private *hp = &hp_private;
	int cap;

	hp->lba_regs = ioremap(hpa, 256);
	if (!hp->lba_regs)
		return -ENOMEM;

	hp->lba_cap_offset = hp_zx1_lba_find_capability(hp->lba_regs, PCI_CAP_ID_AGP);

	cap = readl(hp->lba_regs+hp->lba_cap_offset) & 0xff;
	if (cap != PCI_CAP_ID_AGP) {
		printk(KERN_ERR PFX "Invalid capability ID 0x%02x at 0x%x\n",
		       cap, hp->lba_cap_offset);
		return -ENODEV;
	}

	return 0;
}

static int
hp_zx1_fetch_size(void)
{
	int size;

	size = hp_private.gart_size / MB(1);
	hp_zx1_sizes[0].size = size;
	agp_bridge->current_size = (void *) &hp_zx1_sizes[0];
	return size;
}

static int
hp_zx1_configure (void)
{
	struct _hp_private *hp = &hp_private;

	agp_bridge->gart_bus_addr = hp->gart_base;
	agp_bridge->capndx = hp->lba_cap_offset;
	agp_bridge->mode = readl(hp->lba_regs+hp->lba_cap_offset+PCI_AGP_STATUS);

	if (hp->io_pdir_owner) {
		writel(virt_to_gart(hp->io_pdir), hp->ioc_regs+HP_ZX1_PDIR_BASE);
		readl(hp->ioc_regs+HP_ZX1_PDIR_BASE);
		writel(hp->io_tlb_ps, hp->ioc_regs+HP_ZX1_TCNFG);
		readl(hp->ioc_regs+HP_ZX1_TCNFG);
		writel((unsigned int)(~(HP_ZX1_IOVA_SIZE-1)), hp->ioc_regs+HP_ZX1_IMASK);
		readl(hp->ioc_regs+HP_ZX1_IMASK);
		writel(hp->iova_base|1, hp->ioc_regs+HP_ZX1_IBASE);
		readl(hp->ioc_regs+HP_ZX1_IBASE);
		writel(hp->iova_base|log2(HP_ZX1_IOVA_SIZE), hp->ioc_regs+HP_ZX1_PCOM);
		readl(hp->ioc_regs+HP_ZX1_PCOM);
	}

	return 0;
}

static void
hp_zx1_cleanup (void)
{
	struct _hp_private *hp = &hp_private;

	if (hp->ioc_regs) {
		if (hp->io_pdir_owner) {
			writeq(0, hp->ioc_regs+HP_ZX1_IBASE);
			readq(hp->ioc_regs+HP_ZX1_IBASE);
		}
		iounmap(hp->ioc_regs);
	}
	if (hp->lba_regs)
		iounmap(hp->lba_regs);
}

static void
hp_zx1_tlbflush (struct agp_memory *mem)
{
	struct _hp_private *hp = &hp_private;

	writeq(hp->gart_base | log2(hp->gart_size), hp->ioc_regs+HP_ZX1_PCOM);
	readq(hp->ioc_regs+HP_ZX1_PCOM);
}

static int
hp_zx1_create_gatt_table (struct agp_bridge_data *bridge)
{
	struct _hp_private *hp = &hp_private;
	int i;

	if (hp->io_pdir_owner) {
		hp->io_pdir = (u64 *) __get_free_pages(GFP_KERNEL,
						get_order(hp->io_pdir_size));
		if (!hp->io_pdir) {
			printk(KERN_ERR PFX "Couldn't allocate contiguous "
				"memory for I/O PDIR\n");
			hp->gatt = NULL;
			hp->gatt_entries = 0;
			return -ENOMEM;
		}
		memset(hp->io_pdir, 0, hp->io_pdir_size);

		hp->gatt = &hp->io_pdir[HP_ZX1_IOVA_TO_PDIR(hp->gart_base)];
	}

	for (i = 0; i < hp->gatt_entries; i++) {
		hp->gatt[i] = (unsigned long) agp_bridge->scratch_page;
	}

	return 0;
}

static int
hp_zx1_free_gatt_table (struct agp_bridge_data *bridge)
{
	struct _hp_private *hp = &hp_private;

	if (hp->io_pdir_owner)
		free_pages((unsigned long) hp->io_pdir,
			    get_order(hp->io_pdir_size));
	else
		hp->gatt[0] = HP_ZX1_SBA_IOMMU_COOKIE;
	return 0;
}

static int
hp_zx1_insert_memory (struct agp_memory *mem, off_t pg_start, int type)
{
	struct _hp_private *hp = &hp_private;
	int i, k;
	off_t j, io_pg_start;
	int io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = hp->io_pages_per_kpage * pg_start;
	io_pg_count = hp->io_pages_per_kpage * mem->page_count;
	if ((io_pg_start + io_pg_count) > hp->gatt_entries) {
		return -EINVAL;
	}

	j = io_pg_start;
	while (j < (io_pg_start + io_pg_count)) {
		if (hp->gatt[j]) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = io_pg_start; i < mem->page_count; i++) {
		unsigned long paddr;

		paddr = mem->memory[i];
		for (k = 0;
		     k < hp->io_pages_per_kpage;
		     k++, j++, paddr += hp->io_page_size) {
			hp->gatt[j] =
				agp_bridge->driver->mask_memory(agp_bridge,
					paddr, type);
		}
	}

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static int
hp_zx1_remove_memory (struct agp_memory *mem, off_t pg_start, int type)
{
	struct _hp_private *hp = &hp_private;
	int i, io_pg_start, io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = hp->io_pages_per_kpage * pg_start;
	io_pg_count = hp->io_pages_per_kpage * mem->page_count;
	for (i = io_pg_start; i < io_pg_count + io_pg_start; i++) {
		hp->gatt[i] = agp_bridge->scratch_page;
	}

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static unsigned long
hp_zx1_mask_memory (struct agp_bridge_data *bridge,
	unsigned long addr, int type)
{
	return HP_ZX1_PDIR_VALID_BIT | addr;
}

static void
hp_zx1_enable (struct agp_bridge_data *bridge, u32 mode)
{
	struct _hp_private *hp = &hp_private;
	u32 command;

	command = readl(hp->lba_regs+hp->lba_cap_offset+PCI_AGP_STATUS);
	command = agp_collect_device_status(bridge, mode, command);
	command |= 0x00000100;

	writel(command, hp->lba_regs+hp->lba_cap_offset+PCI_AGP_COMMAND);

	agp_device_command(command, (mode & AGP8X_MODE) != 0);
}

const struct agp_bridge_driver hp_zx1_driver = {
	.owner			= THIS_MODULE,
	.size_type		= FIXED_APER_SIZE,
	.configure		= hp_zx1_configure,
	.fetch_size		= hp_zx1_fetch_size,
	.cleanup		= hp_zx1_cleanup,
	.tlb_flush		= hp_zx1_tlbflush,
	.mask_memory		= hp_zx1_mask_memory,
	.masks			= hp_zx1_masks,
	.agp_enable		= hp_zx1_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= hp_zx1_create_gatt_table,
	.free_gatt_table	= hp_zx1_free_gatt_table,
	.insert_memory		= hp_zx1_insert_memory,
	.remove_memory		= hp_zx1_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
	.cant_use_aperture	= 1,
};

static int __init
hp_zx1_setup (u64 ioc_hpa, u64 lba_hpa)
{
	struct agp_bridge_data *bridge;
	int error = 0;

	error = hp_zx1_ioc_init(ioc_hpa);
	if (error)
		goto fail;

	error = hp_zx1_lba_init(lba_hpa);
	if (error)
		goto fail;

	bridge = agp_alloc_bridge();
	if (!bridge) {
		error = -ENOMEM;
		goto fail;
	}
	bridge->driver = &hp_zx1_driver;

	fake_bridge_dev.vendor = PCI_VENDOR_ID_HP;
	fake_bridge_dev.device = PCI_DEVICE_ID_HP_PCIX_LBA;
	bridge->dev = &fake_bridge_dev;

	error = agp_add_bridge(bridge);
  fail:
	if (error)
		hp_zx1_cleanup();
	return error;
}

static acpi_status __init
zx1_gart_probe (acpi_handle obj, u32 depth, void *context, void **ret)
{
	acpi_handle handle, parent;
	acpi_status status;
	struct acpi_buffer buffer;
	struct acpi_device_info *info;
	u64 lba_hpa, sba_hpa, length;
	int match;

	status = hp_acpi_csr_space(obj, &lba_hpa, &length);
	if (ACPI_FAILURE(status))
		return AE_OK; /* keep looking for another bridge */

	/* Look for an enclosing IOC scope and find its CSR space */
	handle = obj;
	do {
		buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
		status = acpi_get_object_info(handle, &buffer);
		if (ACPI_SUCCESS(status)) {
			/* TBD check _CID also */
			info = buffer.pointer;
			info->hardware_id.value[sizeof(info->hardware_id)-1] = '\0';
			match = (strcmp(info->hardware_id.value, "HWP0001") == 0);
			kfree(info);
			if (match) {
				status = hp_acpi_csr_space(handle, &sba_hpa, &length);
				if (ACPI_SUCCESS(status))
					break;
				else {
					printk(KERN_ERR PFX "Detected HP ZX1 "
					       "AGP LBA but no IOC.\n");
					return AE_OK;
				}
			}
		}

		status = acpi_get_parent(handle, &parent);
		handle = parent;
	} while (ACPI_SUCCESS(status));

	if (hp_zx1_setup(sba_hpa + HP_ZX1_IOC_OFFSET, lba_hpa))
		return AE_OK;

	printk(KERN_INFO PFX "Detected HP ZX1 %s AGP chipset (ioc=%lx, lba=%lx)\n",
		(char *) context, sba_hpa + HP_ZX1_IOC_OFFSET, lba_hpa);

	hp_zx1_gart_found = 1;
	return AE_CTRL_TERMINATE; /* we only support one bridge; quit looking */
}

static int __init
agp_hp_init (void)
{
	if (agp_off)
		return -EINVAL;

	acpi_get_devices("HWP0003", zx1_gart_probe, "HWP0003", NULL);
	if (hp_zx1_gart_found)
		return 0;

	acpi_get_devices("HWP0007", zx1_gart_probe, "HWP0007", NULL);
	if (hp_zx1_gart_found)
		return 0;

	return -ENODEV;
}

static void __exit
agp_hp_cleanup (void)
{
}

module_init(agp_hp_init);
module_exit(agp_hp_cleanup);

MODULE_LICENSE("GPL and additional rights");
