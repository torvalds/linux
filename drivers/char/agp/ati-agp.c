/*
 * ATi AGPGART routines.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/agp_backend.h>
#include <asm/agp.h>
#include "agp.h"

#define ATI_GART_MMBASE_ADDR	0x14
#define ATI_RS100_APSIZE	0xac
#define ATI_RS100_IG_AGPMODE	0xb0
#define ATI_RS300_APSIZE	0xf8
#define ATI_RS300_IG_AGPMODE	0xfc
#define ATI_GART_FEATURE_ID		0x00
#define ATI_GART_BASE			0x04
#define ATI_GART_CACHE_SZBASE		0x08
#define ATI_GART_CACHE_CNTRL		0x0c
#define ATI_GART_CACHE_ENTRY_CNTRL	0x10


static const struct aper_size_info_lvl2 ati_generic_sizes[7] =
{
	{2048, 524288, 0x0000000c},
	{1024, 262144, 0x0000000a},
	{512, 131072, 0x00000008},
	{256, 65536, 0x00000006},
	{128, 32768, 0x00000004},
	{64, 16384, 0x00000002},
	{32, 8192, 0x00000000}
};

static struct gatt_mask ati_generic_masks[] =
{
	{ .mask = 1, .type = 0}
};


struct ati_page_map {
	unsigned long *real;
	unsigned long __iomem *remapped;
};

static struct _ati_generic_private {
	volatile u8 __iomem *registers;
	struct ati_page_map **gatt_pages;
	int num_tables;
} ati_generic_private;

static int ati_create_page_map(struct ati_page_map *page_map)
{
	int i, err = 0;

	page_map->real = (unsigned long *) __get_free_page(GFP_KERNEL);
	if (page_map->real == NULL)
		return -ENOMEM;

	set_memory_uc((unsigned long)page_map->real, 1);
	err = map_page_into_agp(virt_to_page(page_map->real));
	page_map->remapped = page_map->real;

	for (i = 0; i < PAGE_SIZE / sizeof(unsigned long); i++) {
		writel(agp_bridge->scratch_page, page_map->remapped+i);
		readl(page_map->remapped+i);	/* PCI Posting. */
	}

	return 0;
}


static void ati_free_page_map(struct ati_page_map *page_map)
{
	unmap_page_from_agp(virt_to_page(page_map->real));
	set_memory_wb((unsigned long)page_map->real, 1);
	free_page((unsigned long) page_map->real);
}


static void ati_free_gatt_pages(void)
{
	int i;
	struct ati_page_map **tables;
	struct ati_page_map *entry;

	tables = ati_generic_private.gatt_pages;
	for (i = 0; i < ati_generic_private.num_tables; i++) {
		entry = tables[i];
		if (entry != NULL) {
			if (entry->real != NULL)
				ati_free_page_map(entry);
			kfree(entry);
		}
	}
	kfree(tables);
}


static int ati_create_gatt_pages(int nr_tables)
{
	struct ati_page_map **tables;
	struct ati_page_map *entry;
	int retval = 0;
	int i;

	tables = kzalloc((nr_tables + 1) * sizeof(struct ati_page_map *),GFP_KERNEL);
	if (tables == NULL)
		return -ENOMEM;

	for (i = 0; i < nr_tables; i++) {
		entry = kzalloc(sizeof(struct ati_page_map), GFP_KERNEL);
		tables[i] = entry;
		if (entry == NULL) {
			retval = -ENOMEM;
			break;
		}
		retval = ati_create_page_map(entry);
		if (retval != 0)
			break;
	}
	ati_generic_private.num_tables = i;
	ati_generic_private.gatt_pages = tables;

	if (retval != 0)
		ati_free_gatt_pages();

	return retval;
}

static int is_r200(void)
{
	if ((agp_bridge->dev->device == PCI_DEVICE_ID_ATI_RS100) ||
	    (agp_bridge->dev->device == PCI_DEVICE_ID_ATI_RS200) ||
	    (agp_bridge->dev->device == PCI_DEVICE_ID_ATI_RS200_B) ||
	    (agp_bridge->dev->device == PCI_DEVICE_ID_ATI_RS250))
		return 1;
	return 0;
}

static int ati_fetch_size(void)
{
	int i;
	u32 temp;
	struct aper_size_info_lvl2 *values;

	if (is_r200())
		pci_read_config_dword(agp_bridge->dev, ATI_RS100_APSIZE, &temp);
	else
		pci_read_config_dword(agp_bridge->dev, ATI_RS300_APSIZE, &temp);

	temp = (temp & 0x0000000e);
	values = A_SIZE_LVL2(agp_bridge->driver->aperture_sizes);
	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void ati_tlbflush(struct agp_memory * mem)
{
	writel(1, ati_generic_private.registers+ATI_GART_CACHE_CNTRL);
	readl(ati_generic_private.registers+ATI_GART_CACHE_CNTRL);	/* PCI Posting. */
}

static void ati_cleanup(void)
{
	struct aper_size_info_lvl2 *previous_size;
	u32 temp;

	previous_size = A_SIZE_LVL2(agp_bridge->previous_size);

	/* Write back the previous size and disable gart translation */
	if (is_r200()) {
		pci_read_config_dword(agp_bridge->dev, ATI_RS100_APSIZE, &temp);
		temp = ((temp & ~(0x0000000f)) | previous_size->size_value);
		pci_write_config_dword(agp_bridge->dev, ATI_RS100_APSIZE, temp);
	} else {
		pci_read_config_dword(agp_bridge->dev, ATI_RS300_APSIZE, &temp);
		temp = ((temp & ~(0x0000000f)) | previous_size->size_value);
		pci_write_config_dword(agp_bridge->dev, ATI_RS300_APSIZE, temp);
	}
	iounmap((volatile u8 __iomem *)ati_generic_private.registers);
}


static int ati_configure(void)
{
	u32 temp;

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge->dev, ATI_GART_MMBASE_ADDR, &temp);
	temp = (temp & 0xfffff000);
	ati_generic_private.registers = (volatile u8 __iomem *) ioremap(temp, 4096);

	if (!ati_generic_private.registers)
		return -ENOMEM;

	if (is_r200())
		pci_write_config_dword(agp_bridge->dev, ATI_RS100_IG_AGPMODE, 0x20000);
	else
		pci_write_config_dword(agp_bridge->dev, ATI_RS300_IG_AGPMODE, 0x20000);

	/* address to map too */
	/*
	pci_read_config_dword(agp_bridge.dev, AGP_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	printk(KERN_INFO PFX "IGP320 gart_bus_addr: %x\n", agp_bridge.gart_bus_addr);
	*/
	writel(0x60000, ati_generic_private.registers+ATI_GART_FEATURE_ID);
	readl(ati_generic_private.registers+ATI_GART_FEATURE_ID);	/* PCI Posting.*/

	/* SIGNALED_SYSTEM_ERROR @ NB_STATUS */
	pci_read_config_dword(agp_bridge->dev, 4, &temp);
	pci_write_config_dword(agp_bridge->dev, 4, temp | (1<<14));

	/* Write out the address of the gatt table */
	writel(agp_bridge->gatt_bus_addr, ati_generic_private.registers+ATI_GART_BASE);
	readl(ati_generic_private.registers+ATI_GART_BASE);	/* PCI Posting. */

	return 0;
}


#ifdef CONFIG_PM
static int agp_ati_suspend(struct pci_dev *dev, pm_message_t state)
{
	pci_save_state(dev);
	pci_set_power_state(dev, 3);

	return 0;
}

static int agp_ati_resume(struct pci_dev *dev)
{
	pci_set_power_state(dev, 0);
	pci_restore_state(dev);

	return ati_configure();
}
#endif

/*
 *Since we don't need contiguous memory we just try
 * to get the gatt table once
 */

#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr) - \
	GET_PAGE_DIR_OFF(agp_bridge->gart_bus_addr))
#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12)
#undef  GET_GATT
#define GET_GATT(addr) (ati_generic_private.gatt_pages[\
	GET_PAGE_DIR_IDX(addr)]->remapped)

static int ati_insert_memory(struct agp_memory * mem,
			     off_t pg_start, int type)
{
	int i, j, num_entries;
	unsigned long __iomem *cur_gatt;
	unsigned long addr;

	num_entries = A_SIZE_LVL2(agp_bridge->current_size)->num_entries;

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;
	while (j < (pg_start + mem->page_count)) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		if (!PGE_EMPTY(agp_bridge,readl(cur_gatt+GET_GATT_OFF(addr))))
			return -EBUSY;
		j++;
	}

	if (!mem->is_flushed) {
		/*CACHE_FLUSH(); */
		global_cache_flush();
		mem->is_flushed = true;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		writel(agp_bridge->driver->mask_memory(agp_bridge,
			mem->memory[i], mem->type), cur_gatt+GET_GATT_OFF(addr));
		readl(cur_gatt+GET_GATT_OFF(addr));	/* PCI Posting. */
	}
	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static int ati_remove_memory(struct agp_memory * mem, off_t pg_start,
			     int type)
{
	int i;
	unsigned long __iomem *cur_gatt;
	unsigned long addr;

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		addr = (i * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		writel(agp_bridge->scratch_page, cur_gatt+GET_GATT_OFF(addr));
		readl(cur_gatt+GET_GATT_OFF(addr)); /* PCI Posting. */
	}

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static int ati_create_gatt_table(struct agp_bridge_data *bridge)
{
	struct aper_size_info_lvl2 *value;
	struct ati_page_map page_dir;
	unsigned long addr;
	int retval;
	u32 temp;
	int i;
	struct aper_size_info_lvl2 *current_size;

	value = A_SIZE_LVL2(agp_bridge->current_size);
	retval = ati_create_page_map(&page_dir);
	if (retval != 0)
		return retval;

	retval = ati_create_gatt_pages(value->num_entries / 1024);
	if (retval != 0) {
		ati_free_page_map(&page_dir);
		return retval;
	}

	agp_bridge->gatt_table_real = (u32 *)page_dir.real;
	agp_bridge->gatt_table = (u32 __iomem *) page_dir.remapped;
	agp_bridge->gatt_bus_addr = virt_to_gart(page_dir.real);

	/* Write out the size register */
	current_size = A_SIZE_LVL2(agp_bridge->current_size);

	if (is_r200()) {
		pci_read_config_dword(agp_bridge->dev, ATI_RS100_APSIZE, &temp);
		temp = (((temp & ~(0x0000000e)) | current_size->size_value)
			| 0x00000001);
		pci_write_config_dword(agp_bridge->dev, ATI_RS100_APSIZE, temp);
		pci_read_config_dword(agp_bridge->dev, ATI_RS100_APSIZE, &temp);
	} else {
		pci_read_config_dword(agp_bridge->dev, ATI_RS300_APSIZE, &temp);
		temp = (((temp & ~(0x0000000e)) | current_size->size_value)
			| 0x00000001);
		pci_write_config_dword(agp_bridge->dev, ATI_RS300_APSIZE, temp);
		pci_read_config_dword(agp_bridge->dev, ATI_RS300_APSIZE, &temp);
	}

	/*
	 * Get the address for the gart region.
	 * This is a bus address even on the alpha, b/c its
	 * used to program the agp master not the cpu
	 */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	agp_bridge->gart_bus_addr = addr;

	/* Calculate the agp offset */
	for (i = 0; i < value->num_entries / 1024; i++, addr += 0x00400000) {
		writel(virt_to_gart(ati_generic_private.gatt_pages[i]->real) | 1,
			page_dir.remapped+GET_PAGE_DIR_OFF(addr));
		readl(page_dir.remapped+GET_PAGE_DIR_OFF(addr));	/* PCI Posting. */
	}

	return 0;
}

static int ati_free_gatt_table(struct agp_bridge_data *bridge)
{
	struct ati_page_map page_dir;

	page_dir.real = (unsigned long *)agp_bridge->gatt_table_real;
	page_dir.remapped = (unsigned long __iomem *)agp_bridge->gatt_table;

	ati_free_gatt_pages();
	ati_free_page_map(&page_dir);
	return 0;
}

static const struct agp_bridge_driver ati_generic_bridge = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= ati_generic_sizes,
	.size_type		= LVL2_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= ati_configure,
	.fetch_size		= ati_fetch_size,
	.cleanup		= ati_cleanup,
	.tlb_flush		= ati_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= ati_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= ati_create_gatt_table,
	.free_gatt_table	= ati_free_gatt_table,
	.insert_memory		= ati_insert_memory,
	.remove_memory		= ati_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages	= agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages	= agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};


static struct agp_device_ids ati_agp_device_ids[] __devinitdata =
{
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS100,
		.chipset_name	= "IGP320/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS200,
		.chipset_name	= "IGP330/340/345/350/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS200_B,
		.chipset_name	= "IGP345M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS250,
		.chipset_name	= "IGP7000/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS300_100,
		.chipset_name	= "IGP9100/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS300_133,
		.chipset_name	= "IGP9100/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS300_166,
		.chipset_name	= "IGP9100/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS300_200,
		.chipset_name	= "IGP9100/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS350_133,
		.chipset_name	= "IGP9000/M",
	},
	{
		.device_id	= PCI_DEVICE_ID_ATI_RS350_200,
		.chipset_name	= "IGP9100/M",
	},
	{ }, /* dummy final entry, always present */
};

static int __devinit agp_ati_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = ati_agp_device_ids;
	struct agp_bridge_data *bridge;
	u8 cap_ptr;
	int j;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	/* probe for known chipsets */
	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id)
			goto found;
	}

	dev_err(&pdev->dev, "unsupported Ati chipset [%04x/%04x])\n",
		pdev->vendor, pdev->device);
	return -ENODEV;

found:
	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	bridge->driver = &ati_generic_bridge;

	dev_info(&pdev->dev, "Ati %s chipset\n", devs[j].chipset_name);

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			bridge->capndx+PCI_AGP_STATUS,
			&bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_ati_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_ati_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_ATI,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_ati_pci_table);

static struct pci_driver agp_ati_pci_driver = {
	.name		= "agpgart-ati",
	.id_table	= agp_ati_pci_table,
	.probe		= agp_ati_probe,
	.remove		= agp_ati_remove,
#ifdef CONFIG_PM
	.suspend	= agp_ati_suspend,
	.resume		= agp_ati_resume,
#endif
};

static int __init agp_ati_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_ati_pci_driver);
}

static void __exit agp_ati_cleanup(void)
{
	pci_unregister_driver(&agp_ati_pci_driver);
}

module_init(agp_ati_init);
module_exit(agp_ati_cleanup);

MODULE_AUTHOR("Dave Jones <davej@redhat.com>");
MODULE_LICENSE("GPL and additional rights");

