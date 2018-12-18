/*
 * UniNorth AGPGART routines.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/agp_backend.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <asm/uninorth.h>
#include <asm/prom.h>
#include <asm/pmac_feature.h>
#include "agp.h"

/*
 * NOTES for uninorth3 (G5 AGP) supports :
 *
 * There maybe also possibility to have bigger cache line size for
 * agp (see pmac_pci.c and look for cache line). Need to be investigated
 * by someone.
 *
 * PAGE size are hardcoded but this may change, see asm/page.h.
 *
 * Jerome Glisse <j.glisse@gmail.com>
 */
static int uninorth_rev;
static int is_u3;
static u32 scratch_value;

#define DEFAULT_APERTURE_SIZE 256
#define DEFAULT_APERTURE_STRING "256"
static char *aperture = NULL;

static int uninorth_fetch_size(void)
{
	int i, size = 0;
	struct aper_size_info_32 *values =
	    A_SIZE_32(agp_bridge->driver->aperture_sizes);

	if (aperture) {
		char *save = aperture;

		size = memparse(aperture, &aperture) >> 20;
		aperture = save;

		for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++)
			if (size == values[i].size)
				break;

		if (i == agp_bridge->driver->num_aperture_sizes) {
			dev_err(&agp_bridge->dev->dev, "invalid aperture size, "
				"using default\n");
			size = 0;
			aperture = NULL;
		}
	}

	if (!size) {
		for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++)
			if (values[i].size == DEFAULT_APERTURE_SIZE)
				break;
	}

	agp_bridge->previous_size =
	    agp_bridge->current_size = (void *)(values + i);
	agp_bridge->aperture_size_idx = i;
	return values[i].size;
}

static void uninorth_tlbflush(struct agp_memory *mem)
{
	u32 ctrl = UNI_N_CFG_GART_ENABLE;

	if (is_u3)
		ctrl |= U3_N_CFG_GART_PERFRD;
	pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL,
			       ctrl | UNI_N_CFG_GART_INVAL);
	pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL, ctrl);

	if (!mem && uninorth_rev <= 0x30) {
		pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL,
				       ctrl | UNI_N_CFG_GART_2xRESET);
		pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL,
				       ctrl);
	}
}

static void uninorth_cleanup(void)
{
	u32 tmp;

	pci_read_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL, &tmp);
	if (!(tmp & UNI_N_CFG_GART_ENABLE))
		return;
	tmp |= UNI_N_CFG_GART_INVAL;
	pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL, tmp);
	pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL, 0);

	if (uninorth_rev <= 0x30) {
		pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL,
				       UNI_N_CFG_GART_2xRESET);
		pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_GART_CTRL,
				       0);
	}
}

static int uninorth_configure(void)
{
	struct aper_size_info_32 *current_size;

	current_size = A_SIZE_32(agp_bridge->current_size);

	dev_info(&agp_bridge->dev->dev, "configuring for size idx: %d\n",
		 current_size->size_value);

	/* aperture size and gatt addr */
	pci_write_config_dword(agp_bridge->dev,
		UNI_N_CFG_GART_BASE,
		(agp_bridge->gatt_bus_addr & 0xfffff000)
			| current_size->size_value);

	/* HACK ALERT
	 * UniNorth seem to be buggy enough not to handle properly when
	 * the AGP aperture isn't mapped at bus physical address 0
	 */
	agp_bridge->gart_bus_addr = 0;
#ifdef CONFIG_PPC64
	/* Assume U3 or later on PPC64 systems */
	/* high 4 bits of GART physical address go in UNI_N_CFG_AGP_BASE */
	pci_write_config_dword(agp_bridge->dev, UNI_N_CFG_AGP_BASE,
			       (agp_bridge->gatt_bus_addr >> 32) & 0xf);
#else
	pci_write_config_dword(agp_bridge->dev,
		UNI_N_CFG_AGP_BASE, agp_bridge->gart_bus_addr);
#endif

	if (is_u3) {
		pci_write_config_dword(agp_bridge->dev,
				       UNI_N_CFG_GART_DUMMY_PAGE,
				       page_to_phys(agp_bridge->scratch_page_page) >> 12);
	}

	return 0;
}

static int uninorth_insert_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	int i, num_entries;
	void *temp;
	u32 *gp;
	int mask_type;

	if (type != mem->type)
		return -EINVAL;

	mask_type = agp_bridge->driver->agp_type_to_mask_type(agp_bridge, type);
	if (mask_type != 0) {
		/* We know nothing of memory types */
		return -EINVAL;
	}

	if (mem->page_count == 0)
		return 0;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_32(temp)->num_entries;

	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	gp = (u32 *) &agp_bridge->gatt_table[pg_start];
	for (i = 0; i < mem->page_count; ++i) {
		if (gp[i] != scratch_value) {
			dev_info(&agp_bridge->dev->dev,
				 "uninorth_insert_memory: entry 0x%x occupied (%x)\n",
				 i, gp[i]);
			return -EBUSY;
		}
	}

	for (i = 0; i < mem->page_count; i++) {
		if (is_u3)
			gp[i] = (page_to_phys(mem->pages[i]) >> PAGE_SHIFT) | 0x80000000UL;
		else
			gp[i] =	cpu_to_le32((page_to_phys(mem->pages[i]) & 0xFFFFF000UL) |
					    0x1UL);
		flush_dcache_range((unsigned long)__va(page_to_phys(mem->pages[i])),
				   (unsigned long)__va(page_to_phys(mem->pages[i]))+0x1000);
	}
	mb();
	uninorth_tlbflush(mem);

	return 0;
}

static int uninorth_remove_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	size_t i;
	u32 *gp;
	int mask_type;

	if (type != mem->type)
		return -EINVAL;

	mask_type = agp_bridge->driver->agp_type_to_mask_type(agp_bridge, type);
	if (mask_type != 0) {
		/* We know nothing of memory types */
		return -EINVAL;
	}

	if (mem->page_count == 0)
		return 0;

	gp = (u32 *) &agp_bridge->gatt_table[pg_start];
	for (i = 0; i < mem->page_count; ++i) {
		gp[i] = scratch_value;
	}
	mb();
	uninorth_tlbflush(mem);

	return 0;
}

static void uninorth_agp_enable(struct agp_bridge_data *bridge, u32 mode)
{
	u32 command, scratch, status;
	int timeout;

	pci_read_config_dword(bridge->dev,
			      bridge->capndx + PCI_AGP_STATUS,
			      &status);

	command = agp_collect_device_status(bridge, mode, status);
	command |= PCI_AGP_COMMAND_AGP;

	if (uninorth_rev == 0x21) {
		/*
		 * Darwin disable AGP 4x on this revision, thus we
		 * may assume it's broken. This is an AGP2 controller.
		 */
		command &= ~AGPSTAT2_4X;
	}

	if ((uninorth_rev >= 0x30) && (uninorth_rev <= 0x33)) {
		/*
		 * We need to set REQ_DEPTH to 7 for U3 versions 1.0, 2.1,
		 * 2.2 and 2.3, Darwin do so.
		 */
		if ((command >> AGPSTAT_RQ_DEPTH_SHIFT) > 7)
			command = (command & ~AGPSTAT_RQ_DEPTH)
				| (7 << AGPSTAT_RQ_DEPTH_SHIFT);
	}

	uninorth_tlbflush(NULL);

	timeout = 0;
	do {
		pci_write_config_dword(bridge->dev,
				       bridge->capndx + PCI_AGP_COMMAND,
				       command);
		pci_read_config_dword(bridge->dev,
				      bridge->capndx + PCI_AGP_COMMAND,
				       &scratch);
	} while ((scratch & PCI_AGP_COMMAND_AGP) == 0 && ++timeout < 1000);
	if ((scratch & PCI_AGP_COMMAND_AGP) == 0)
		dev_err(&bridge->dev->dev, "can't write UniNorth AGP "
			"command register\n");

	if (uninorth_rev >= 0x30) {
		/* This is an AGP V3 */
		agp_device_command(command, (status & AGPSTAT_MODE_3_0) != 0);
	} else {
		/* AGP V2 */
		agp_device_command(command, false);
	}

	uninorth_tlbflush(NULL);
}

#ifdef CONFIG_PM
/*
 * These Power Management routines are _not_ called by the normal PCI PM layer,
 * but directly by the video driver through function pointers in the device
 * tree.
 */
static int agp_uninorth_suspend(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge;
	u32 cmd;
	u8 agp;
	struct pci_dev *device = NULL;

	bridge = agp_find_bridge(pdev);
	if (bridge == NULL)
		return -ENODEV;

	/* Only one suspend supported */
	if (bridge->dev_private_data)
		return 0;

	/* turn off AGP on the video chip, if it was enabled */
	for_each_pci_dev(device) {
		/* Don't touch the bridge yet, device first */
		if (device == pdev)
			continue;
		/* Only deal with devices on the same bus here, no Mac has a P2P
		 * bridge on the AGP port, and mucking around the entire PCI
		 * tree is source of problems on some machines because of a bug
		 * in some versions of pci_find_capability() when hitting a dead
		 * device
		 */
		if (device->bus != pdev->bus)
			continue;
		agp = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (!agp)
			continue;
		pci_read_config_dword(device, agp + PCI_AGP_COMMAND, &cmd);
		if (!(cmd & PCI_AGP_COMMAND_AGP))
			continue;
		dev_info(&pdev->dev, "disabling AGP on device %s\n",
			 pci_name(device));
		cmd &= ~PCI_AGP_COMMAND_AGP;
		pci_write_config_dword(device, agp + PCI_AGP_COMMAND, cmd);
	}

	/* turn off AGP on the bridge */
	agp = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	pci_read_config_dword(pdev, agp + PCI_AGP_COMMAND, &cmd);
	bridge->dev_private_data = (void *)(long)cmd;
	if (cmd & PCI_AGP_COMMAND_AGP) {
		dev_info(&pdev->dev, "disabling AGP on bridge\n");
		cmd &= ~PCI_AGP_COMMAND_AGP;
		pci_write_config_dword(pdev, agp + PCI_AGP_COMMAND, cmd);
	}
	/* turn off the GART */
	uninorth_cleanup();

	return 0;
}

static int agp_uninorth_resume(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge;
	u32 command;

	bridge = agp_find_bridge(pdev);
	if (bridge == NULL)
		return -ENODEV;

	command = (long)bridge->dev_private_data;
	bridge->dev_private_data = NULL;
	if (!(command & PCI_AGP_COMMAND_AGP))
		return 0;

	uninorth_agp_enable(bridge, command);

	return 0;
}
#endif /* CONFIG_PM */

static struct {
	struct page **pages_arr;
} uninorth_priv;

static int uninorth_create_gatt_table(struct agp_bridge_data *bridge)
{
	char *table;
	char *table_end;
	int size;
	int page_order;
	int num_entries;
	int i;
	void *temp;
	struct page *page;

	/* We can't handle 2 level gatt's */
	if (bridge->driver->size_type == LVL2_APER_SIZE)
		return -EINVAL;

	table = NULL;
	i = bridge->aperture_size_idx;
	temp = bridge->current_size;
	size = page_order = num_entries = 0;

	do {
		size = A_SIZE_32(temp)->size;
		page_order = A_SIZE_32(temp)->page_order;
		num_entries = A_SIZE_32(temp)->num_entries;

		table = (char *) __get_free_pages(GFP_KERNEL, page_order);

		if (table == NULL) {
			i++;
			bridge->current_size = A_IDX32(bridge);
		} else {
			bridge->aperture_size_idx = i;
		}
	} while (!table && (i < bridge->driver->num_aperture_sizes));

	if (table == NULL)
		return -ENOMEM;

	uninorth_priv.pages_arr = kmalloc_array(1 << page_order,
						sizeof(struct page *),
						GFP_KERNEL);
	if (uninorth_priv.pages_arr == NULL)
		goto enomem;

	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (page = virt_to_page(table), i = 0; page <= virt_to_page(table_end);
	     page++, i++) {
		SetPageReserved(page);
		uninorth_priv.pages_arr[i] = page;
	}

	bridge->gatt_table_real = (u32 *) table;
	/* Need to clear out any dirty data still sitting in caches */
	flush_dcache_range((unsigned long)table,
			   (unsigned long)table_end + 1);
	bridge->gatt_table = vmap(uninorth_priv.pages_arr, (1 << page_order), 0, PAGE_KERNEL_NCG);

	if (bridge->gatt_table == NULL)
		goto enomem;

	bridge->gatt_bus_addr = virt_to_phys(table);

	if (is_u3)
		scratch_value = (page_to_phys(agp_bridge->scratch_page_page) >> PAGE_SHIFT) | 0x80000000UL;
	else
		scratch_value =	cpu_to_le32((page_to_phys(agp_bridge->scratch_page_page) & 0xFFFFF000UL) |
				0x1UL);
	for (i = 0; i < num_entries; i++)
		bridge->gatt_table[i] = scratch_value;

	return 0;

enomem:
	kfree(uninorth_priv.pages_arr);
	if (table)
		free_pages((unsigned long)table, page_order);
	return -ENOMEM;
}

static int uninorth_free_gatt_table(struct agp_bridge_data *bridge)
{
	int page_order;
	char *table, *table_end;
	void *temp;
	struct page *page;

	temp = bridge->current_size;
	page_order = A_SIZE_32(temp)->page_order;

	/* Do not worry about freeing memory, because if this is
	 * called, then all agp memory is deallocated and removed
	 * from the table.
	 */

	vunmap(bridge->gatt_table);
	kfree(uninorth_priv.pages_arr);
	table = (char *) bridge->gatt_table_real;
	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
		ClearPageReserved(page);

	free_pages((unsigned long) bridge->gatt_table_real, page_order);

	return 0;
}

static void null_cache_flush(void)
{
	mb();
}

/* Setup function */

static const struct aper_size_info_32 uninorth_sizes[] =
{
	{256, 65536, 6, 64},
	{128, 32768, 5, 32},
	{64, 16384, 4, 16},
	{32, 8192, 3, 8},
	{16, 4096, 2, 4},
	{8, 2048, 1, 2},
	{4, 1024, 0, 1}
};

/*
 * Not sure that u3 supports that high aperture sizes but it
 * would strange if it did not :)
 */
static const struct aper_size_info_32 u3_sizes[] =
{
	{512, 131072, 7, 128},
	{256, 65536, 6, 64},
	{128, 32768, 5, 32},
	{64, 16384, 4, 16},
	{32, 8192, 3, 8},
	{16, 4096, 2, 4},
	{8, 2048, 1, 2},
	{4, 1024, 0, 1}
};

const struct agp_bridge_driver uninorth_agp_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= (void *)uninorth_sizes,
	.size_type		= U32_APER_SIZE,
	.num_aperture_sizes	= ARRAY_SIZE(uninorth_sizes),
	.configure		= uninorth_configure,
	.fetch_size		= uninorth_fetch_size,
	.cleanup		= uninorth_cleanup,
	.tlb_flush		= uninorth_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= NULL,
	.cache_flush		= null_cache_flush,
	.agp_enable		= uninorth_agp_enable,
	.create_gatt_table	= uninorth_create_gatt_table,
	.free_gatt_table	= uninorth_free_gatt_table,
	.insert_memory		= uninorth_insert_memory,
	.remove_memory		= uninorth_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages	= agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages	= agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
	.cant_use_aperture	= true,
	.needs_scratch_page	= true,
};

const struct agp_bridge_driver u3_agp_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= (void *)u3_sizes,
	.size_type		= U32_APER_SIZE,
	.num_aperture_sizes	= ARRAY_SIZE(u3_sizes),
	.configure		= uninorth_configure,
	.fetch_size		= uninorth_fetch_size,
	.cleanup		= uninorth_cleanup,
	.tlb_flush		= uninorth_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= NULL,
	.cache_flush		= null_cache_flush,
	.agp_enable		= uninorth_agp_enable,
	.create_gatt_table	= uninorth_create_gatt_table,
	.free_gatt_table	= uninorth_free_gatt_table,
	.insert_memory		= uninorth_insert_memory,
	.remove_memory		= uninorth_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages	= agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages	= agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
	.cant_use_aperture	= true,
	.needs_scratch_page	= true,
};

static struct agp_device_ids uninorth_agp_device_ids[] = {
	{
		.device_id	= PCI_DEVICE_ID_APPLE_UNI_N_AGP,
		.chipset_name	= "UniNorth",
	},
	{
		.device_id	= PCI_DEVICE_ID_APPLE_UNI_N_AGP_P,
		.chipset_name	= "UniNorth/Pangea",
	},
	{
		.device_id	= PCI_DEVICE_ID_APPLE_UNI_N_AGP15,
		.chipset_name	= "UniNorth 1.5",
	},
	{
		.device_id	= PCI_DEVICE_ID_APPLE_UNI_N_AGP2,
		.chipset_name	= "UniNorth 2",
	},
	{
		.device_id	= PCI_DEVICE_ID_APPLE_U3_AGP,
		.chipset_name	= "U3",
	},
	{
		.device_id	= PCI_DEVICE_ID_APPLE_U3L_AGP,
		.chipset_name	= "U3L",
	},
	{
		.device_id	= PCI_DEVICE_ID_APPLE_U3H_AGP,
		.chipset_name	= "U3H",
	},
	{
		.device_id	= PCI_DEVICE_ID_APPLE_IPID2_AGP,
		.chipset_name	= "UniNorth/Intrepid2",
	},
};

static int agp_uninorth_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = uninorth_agp_device_ids;
	struct agp_bridge_data *bridge;
	struct device_node *uninorth_node;
	u8 cap_ptr;
	int j;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0)
		return -ENODEV;

	/* probe for known chipsets */
	for (j = 0; devs[j].chipset_name != NULL; ++j) {
		if (pdev->device == devs[j].device_id) {
			dev_info(&pdev->dev, "Apple %s chipset\n",
				 devs[j].chipset_name);
			goto found;
		}
	}

	dev_err(&pdev->dev, "unsupported Apple chipset [%04x/%04x]\n",
		pdev->vendor, pdev->device);
	return -ENODEV;

 found:
	/* Set revision to 0 if we could not read it. */
	uninorth_rev = 0;
	is_u3 = 0;
	/* Locate core99 Uni-N */
	uninorth_node = of_find_node_by_name(NULL, "uni-n");
	/* Locate G5 u3 */
	if (uninorth_node == NULL) {
		is_u3 = 1;
		uninorth_node = of_find_node_by_name(NULL, "u3");
	}
	if (uninorth_node) {
		const int *revprop = of_get_property(uninorth_node,
				"device-rev", NULL);
		if (revprop != NULL)
			uninorth_rev = *revprop & 0x3f;
		of_node_put(uninorth_node);
	}

#ifdef CONFIG_PM
	/* Inform platform of our suspend/resume caps */
	pmac_register_agp_pm(pdev, agp_uninorth_suspend, agp_uninorth_resume);
#endif

	/* Allocate & setup our driver */
	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	if (is_u3)
		bridge->driver = &u3_agp_driver;
	else
		bridge->driver = &uninorth_agp_driver;

	bridge->dev = pdev;
	bridge->capndx = cap_ptr;
	bridge->flags = AGP_ERRATA_FASTWRITES;

	/* Fill in the mode register */
	pci_read_config_dword(pdev, cap_ptr+PCI_AGP_STATUS, &bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void agp_uninorth_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

#ifdef CONFIG_PM
	/* Inform platform of our suspend/resume caps */
	pmac_register_agp_pm(pdev, NULL, NULL);
#endif

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static const struct pci_device_id agp_uninorth_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_APPLE,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_uninorth_pci_table);

static struct pci_driver agp_uninorth_pci_driver = {
	.name		= "agpgart-uninorth",
	.id_table	= agp_uninorth_pci_table,
	.probe		= agp_uninorth_probe,
	.remove		= agp_uninorth_remove,
};

static int __init agp_uninorth_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_uninorth_pci_driver);
}

static void __exit agp_uninorth_cleanup(void)
{
	pci_unregister_driver(&agp_uninorth_pci_driver);
}

module_init(agp_uninorth_init);
module_exit(agp_uninorth_cleanup);

module_param(aperture, charp, 0);
MODULE_PARM_DESC(aperture,
		 "Aperture size, must be power of two between 4MB and an\n"
		 "\t\tupper limit specific to the UniNorth revision.\n"
		 "\t\tDefault: " DEFAULT_APERTURE_STRING "M");

MODULE_AUTHOR("Ben Herrenschmidt & Paul Mackerras");
MODULE_LICENSE("GPL");
