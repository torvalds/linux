/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003-2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * SGI TIOCA AGPGART routines.
 *
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <asm/sn/addrs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/tioca_provider.h>
#include "agp.h"

extern int agp_memory_reserved;
extern uint32_t tioca_gart_found;
extern struct list_head tioca_list;
static struct agp_bridge_data **sgi_tioca_agp_bridges;

/*
 * The aperature size and related information is set up at TIOCA init time.
 * Values for this table will be extracted and filled in at
 * sgi_tioca_fetch_size() time.
 */

static struct aper_size_info_fixed sgi_tioca_sizes[] = {
	{0, 0, 0},
};

static void *sgi_tioca_alloc_page(struct agp_bridge_data *bridge)
{
	struct page *page;
	int nid;
	struct tioca_kernel *info =
	    (struct tioca_kernel *)bridge->dev_private_data;

	nid = info->ca_closest_node;
	page = alloc_pages_node(nid, GFP_KERNEL, 0);
	if (page == NULL) {
		return 0;
	}

	get_page(page);
	SetPageLocked(page);
	atomic_inc(&agp_bridge->current_memory_agp);
	return page_address(page);
}

/*
 * Flush GART tlb's.  Cannot selectively flush based on memory so the mem
 * arg is ignored.
 */

static void sgi_tioca_tlbflush(struct agp_memory *mem)
{
	tioca_tlbflush(mem->bridge->dev_private_data);
}

/*
 * Given an address of a host physical page, turn it into a valid gart
 * entry.
 */
static unsigned long
sgi_tioca_mask_memory(struct agp_bridge_data *bridge,
		      unsigned long addr, int type)
{
	return tioca_physpage_to_gart(addr);
}

static void sgi_tioca_agp_enable(struct agp_bridge_data *bridge, u32 mode)
{
	tioca_fastwrite_enable(bridge->dev_private_data);
}

/*
 * sgi_tioca_configure() doesn't have anything to do since the base CA driver
 * has alreay set up the GART.
 */

static int sgi_tioca_configure(void)
{
	return 0;
}

/*
 * Determine gfx aperature size.  This has already been determined by the
 * CA driver init, so just need to set agp_bridge values accordingly.
 */

static int sgi_tioca_fetch_size(void)
{
	struct tioca_kernel *info =
	    (struct tioca_kernel *)agp_bridge->dev_private_data;

	sgi_tioca_sizes[0].size = info->ca_gfxap_size / MB(1);
	sgi_tioca_sizes[0].num_entries = info->ca_gfxgart_entries;

	return sgi_tioca_sizes[0].size;
}

static int sgi_tioca_create_gatt_table(struct agp_bridge_data *bridge)
{
	struct tioca_kernel *info =
	    (struct tioca_kernel *)bridge->dev_private_data;

	bridge->gatt_table_real = (u32 *) info->ca_gfxgart;
	bridge->gatt_table = bridge->gatt_table_real;
	bridge->gatt_bus_addr = info->ca_gfxgart_base;

	return 0;
}

static int sgi_tioca_free_gatt_table(struct agp_bridge_data *bridge)
{
	return 0;
}

static int sgi_tioca_insert_memory(struct agp_memory *mem, off_t pg_start,
				   int type)
{
	int num_entries;
	size_t i;
	off_t j;
	void *temp;
	struct agp_bridge_data *bridge;
	u64 *table;

	bridge = mem->bridge;
	if (!bridge)
		return -EINVAL;

	table = (u64 *)bridge->gatt_table;

	temp = bridge->current_size;

	switch (bridge->driver->size_type) {
	case U8_APER_SIZE:
		num_entries = A_SIZE_8(temp)->num_entries;
		break;
	case U16_APER_SIZE:
		num_entries = A_SIZE_16(temp)->num_entries;
		break;
	case U32_APER_SIZE:
		num_entries = A_SIZE_32(temp)->num_entries;
		break;
	case FIXED_APER_SIZE:
		num_entries = A_SIZE_FIX(temp)->num_entries;
		break;
	case LVL2_APER_SIZE:
		return -EINVAL;
		break;
	default:
		num_entries = 0;
		break;
	}

	num_entries -= agp_memory_reserved / PAGE_SIZE;
	if (num_entries < 0)
		num_entries = 0;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	while (j < (pg_start + mem->page_count)) {
		if (table[j])
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		bridge->driver->cache_flush();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		table[j] =
		    bridge->driver->mask_memory(bridge, mem->memory[i],
						mem->type);
	}

	bridge->driver->tlb_flush(mem);
	return 0;
}

static int sgi_tioca_remove_memory(struct agp_memory *mem, off_t pg_start,
				   int type)
{
	size_t i;
	struct agp_bridge_data *bridge;
	u64 *table;

	bridge = mem->bridge;
	if (!bridge)
		return -EINVAL;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	table = (u64 *)bridge->gatt_table;

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		table[i] = 0;
	}

	bridge->driver->tlb_flush(mem);
	return 0;
}

static void sgi_tioca_cache_flush(void)
{
}

/*
 * Cleanup.  Nothing to do as the CA driver owns the GART.
 */

static void sgi_tioca_cleanup(void)
{
}

static struct agp_bridge_data *sgi_tioca_find_bridge(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge;

	list_for_each_entry(bridge, &agp_bridges, list) {
		if (bridge->dev->bus == pdev->bus)
			break;
	}
	return bridge;
}

struct agp_bridge_driver sgi_tioca_driver = {
	.owner = THIS_MODULE,
	.size_type = U16_APER_SIZE,
	.configure = sgi_tioca_configure,
	.fetch_size = sgi_tioca_fetch_size,
	.cleanup = sgi_tioca_cleanup,
	.tlb_flush = sgi_tioca_tlbflush,
	.mask_memory = sgi_tioca_mask_memory,
	.agp_enable = sgi_tioca_agp_enable,
	.cache_flush = sgi_tioca_cache_flush,
	.create_gatt_table = sgi_tioca_create_gatt_table,
	.free_gatt_table = sgi_tioca_free_gatt_table,
	.insert_memory = sgi_tioca_insert_memory,
	.remove_memory = sgi_tioca_remove_memory,
	.alloc_by_type = agp_generic_alloc_by_type,
	.free_by_type = agp_generic_free_by_type,
	.agp_alloc_page = sgi_tioca_alloc_page,
	.agp_destroy_page = agp_generic_destroy_page,
	.cant_use_aperture = 1,
	.needs_scratch_page = 0,
	.num_aperture_sizes = 1,
};

static int __devinit agp_sgi_init(void)
{
	unsigned int j;
	struct tioca_kernel *info;
	struct pci_dev *pdev = NULL;

	if (tioca_gart_found)
		printk(KERN_INFO PFX "SGI TIO CA GART driver initialized.\n");
	else
		return 0;

	sgi_tioca_agp_bridges =
	    (struct agp_bridge_data **)kmalloc(tioca_gart_found *
					       sizeof(struct agp_bridge_data *),
					       GFP_KERNEL);

	j = 0;
	list_for_each_entry(info, &tioca_list, ca_list) {
		struct list_head *tmp;
		list_for_each(tmp, info->ca_devices) {
			u8 cap_ptr;
			pdev = pci_dev_b(tmp);
			if (pdev->class != (PCI_CLASS_DISPLAY_VGA << 8))
				continue;
			cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
			if (!cap_ptr)
				continue;
		}
		sgi_tioca_agp_bridges[j] = agp_alloc_bridge();
		printk(KERN_INFO PFX "bridge %d = 0x%p\n", j,
		       sgi_tioca_agp_bridges[j]);
		if (sgi_tioca_agp_bridges[j]) {
			sgi_tioca_agp_bridges[j]->dev = pdev;
			sgi_tioca_agp_bridges[j]->dev_private_data = info;
			sgi_tioca_agp_bridges[j]->driver = &sgi_tioca_driver;
			sgi_tioca_agp_bridges[j]->gart_bus_addr =
			    info->ca_gfxap_base;
			sgi_tioca_agp_bridges[j]->mode = (0x7D << 24) |	/* 126 requests */
			    (0x1 << 9) |	/* SBA supported */
			    (0x1 << 5) |	/* 64-bit addresses supported */
			    (0x1 << 4) |	/* FW supported */
			    (0x1 << 3) |	/* AGP 3.0 mode */
			    0x2;	/* 8x transfer only */
			sgi_tioca_agp_bridges[j]->current_size =
			    sgi_tioca_agp_bridges[j]->previous_size =
			    (void *)&sgi_tioca_sizes[0];
			agp_add_bridge(sgi_tioca_agp_bridges[j]);
		}
		j++;
	}

	agp_find_bridge = &sgi_tioca_find_bridge;
	return 0;
}

static void __devexit agp_sgi_cleanup(void)
{
	if(sgi_tioca_agp_bridges)
		kfree(sgi_tioca_agp_bridges);
	sgi_tioca_agp_bridges=NULL;
}

module_init(agp_sgi_init);
module_exit(agp_sgi_cleanup);

MODULE_LICENSE("GPL and additional rights");
