/*
 * Serverworks AGPGART routines.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/agp_backend.h>
#include "agp.h"

#define SVWRKS_COMMAND		0x04
#define SVWRKS_APSIZE		0x10
#define SVWRKS_MMBASE		0x14
#define SVWRKS_CACHING		0x4b
#define SVWRKS_AGP_ENABLE	0x60
#define SVWRKS_FEATURE		0x68

#define SVWRKS_SIZE_MASK	0xfe000000

/* Memory mapped registers */
#define SVWRKS_GART_CACHE	0x02
#define SVWRKS_GATTBASE		0x04
#define SVWRKS_TLBFLUSH		0x10
#define SVWRKS_POSTFLUSH	0x14
#define SVWRKS_DIRFLUSH		0x0c


struct serverworks_page_map {
	unsigned long *real;
	unsigned long __iomem *remapped;
};

static struct _serverworks_private {
	struct pci_dev *svrwrks_dev;	/* device one */
	volatile u8 __iomem *registers;
	struct serverworks_page_map **gatt_pages;
	int num_tables;
	struct serverworks_page_map scratch_dir;

	int gart_addr_ofs;
	int mm_addr_ofs;
} serverworks_private;

static int serverworks_create_page_map(struct serverworks_page_map *page_map)
{
	int i;

	page_map->real = (unsigned long *) __get_free_page(GFP_KERNEL);
	if (page_map->real == NULL) {
		return -ENOMEM;
	}
	SetPageReserved(virt_to_page(page_map->real));
	global_cache_flush();
	page_map->remapped = ioremap_nocache(virt_to_gart(page_map->real),
					    PAGE_SIZE);
	if (page_map->remapped == NULL) {
		ClearPageReserved(virt_to_page(page_map->real));
		free_page((unsigned long) page_map->real);
		page_map->real = NULL;
		return -ENOMEM;
	}
	global_cache_flush();

	for(i = 0; i < PAGE_SIZE / sizeof(unsigned long); i++)
		writel(agp_bridge->scratch_page, page_map->remapped+i);

	return 0;
}

static void serverworks_free_page_map(struct serverworks_page_map *page_map)
{
	iounmap(page_map->remapped);
	ClearPageReserved(virt_to_page(page_map->real));
	free_page((unsigned long) page_map->real);
}

static void serverworks_free_gatt_pages(void)
{
	int i;
	struct serverworks_page_map **tables;
	struct serverworks_page_map *entry;

	tables = serverworks_private.gatt_pages;
	for(i = 0; i < serverworks_private.num_tables; i++) {
		entry = tables[i];
		if (entry != NULL) {
			if (entry->real != NULL) {
				serverworks_free_page_map(entry);
			}
			kfree(entry);
		}
	}
	kfree(tables);
}

static int serverworks_create_gatt_pages(int nr_tables)
{
	struct serverworks_page_map **tables;
	struct serverworks_page_map *entry;
	int retval = 0;
	int i;

	tables = kzalloc((nr_tables + 1) * sizeof(struct serverworks_page_map *), 
			 GFP_KERNEL);
	if (tables == NULL)
		return -ENOMEM;

	for (i = 0; i < nr_tables; i++) {
		entry = kzalloc(sizeof(struct serverworks_page_map), GFP_KERNEL);
		if (entry == NULL) {
			retval = -ENOMEM;
			break;
		}
		tables[i] = entry;
		retval = serverworks_create_page_map(entry);
		if (retval != 0) break;
	}
	serverworks_private.num_tables = nr_tables;
	serverworks_private.gatt_pages = tables;

	if (retval != 0) serverworks_free_gatt_pages();

	return retval;
}

#define SVRWRKS_GET_GATT(addr) (serverworks_private.gatt_pages[\
	GET_PAGE_DIR_IDX(addr)]->remapped)

#ifndef GET_PAGE_DIR_OFF
#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#endif

#ifndef GET_PAGE_DIR_IDX
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr) - \
	GET_PAGE_DIR_OFF(agp_bridge->gart_bus_addr))
#endif

#ifndef GET_GATT_OFF
#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12)
#endif

static int serverworks_create_gatt_table(struct agp_bridge_data *bridge)
{
	struct aper_size_info_lvl2 *value;
	struct serverworks_page_map page_dir;
	int retval;
	u32 temp;
	int i;

	value = A_SIZE_LVL2(agp_bridge->current_size);
	retval = serverworks_create_page_map(&page_dir);
	if (retval != 0) {
		return retval;
	}
	retval = serverworks_create_page_map(&serverworks_private.scratch_dir);
	if (retval != 0) {
		serverworks_free_page_map(&page_dir);
		return retval;
	}
	/* Create a fake scratch directory */
	for(i = 0; i < 1024; i++) {
		writel(agp_bridge->scratch_page, serverworks_private.scratch_dir.remapped+i);
		writel(virt_to_gart(serverworks_private.scratch_dir.real) | 1, page_dir.remapped+i);
	}

	retval = serverworks_create_gatt_pages(value->num_entries / 1024);
	if (retval != 0) {
		serverworks_free_page_map(&page_dir);
		serverworks_free_page_map(&serverworks_private.scratch_dir);
		return retval;
	}

	agp_bridge->gatt_table_real = (u32 *)page_dir.real;
	agp_bridge->gatt_table = (u32 __iomem *)page_dir.remapped;
	agp_bridge->gatt_bus_addr = virt_to_gart(page_dir.real);

	/* Get the address for the gart region.
	 * This is a bus address even on the alpha, b/c its
	 * used to program the agp master not the cpu
	 */

	pci_read_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,&temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* Calculate the agp offset */	

	for(i = 0; i < value->num_entries / 1024; i++)
		writel(virt_to_gart(serverworks_private.gatt_pages[i]->real)|1, page_dir.remapped+i);

	return 0;
}

static int serverworks_free_gatt_table(struct agp_bridge_data *bridge)
{
	struct serverworks_page_map page_dir;
   
	page_dir.real = (unsigned long *)agp_bridge->gatt_table_real;
	page_dir.remapped = (unsigned long __iomem *)agp_bridge->gatt_table;

	serverworks_free_gatt_pages();
	serverworks_free_page_map(&page_dir);
	serverworks_free_page_map(&serverworks_private.scratch_dir);
	return 0;
}

static int serverworks_fetch_size(void)
{
	int i;
	u32 temp;
	u32 temp2;
	struct aper_size_info_lvl2 *values;

	values = A_SIZE_LVL2(agp_bridge->driver->aperture_sizes);
	pci_read_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,&temp);
	pci_write_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,
					SVWRKS_SIZE_MASK);
	pci_read_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,&temp2);
	pci_write_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,temp);
	temp2 &= SVWRKS_SIZE_MASK;

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp2 == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

/*
 * This routine could be implemented by taking the addresses
 * written to the GATT, and flushing them individually.  However
 * currently it just flushes the whole table.  Which is probably
 * more efficent, since agp_memory blocks can be a large number of
 * entries.
 */
static void serverworks_tlbflush(struct agp_memory *temp)
{
	unsigned long timeout;

	writeb(1, serverworks_private.registers+SVWRKS_POSTFLUSH);
	timeout = jiffies + 3*HZ;
	while (readb(serverworks_private.registers+SVWRKS_POSTFLUSH) == 1) {
		cpu_relax();
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR PFX "TLB post flush took more than 3 seconds\n");
			break;
		}
	}

	writel(1, serverworks_private.registers+SVWRKS_DIRFLUSH);
	timeout = jiffies + 3*HZ;
	while (readl(serverworks_private.registers+SVWRKS_DIRFLUSH) == 1) {
		cpu_relax();
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR PFX "TLB Dir flush took more than 3 seconds\n");
			break;
		}
	}
}

static int serverworks_configure(void)
{
	struct aper_size_info_lvl2 *current_size;
	u32 temp;
	u8 enable_reg;
	u16 cap_reg;

	current_size = A_SIZE_LVL2(agp_bridge->current_size);

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge->dev, serverworks_private.mm_addr_ofs, &temp);
	temp = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	serverworks_private.registers = (volatile u8 __iomem *) ioremap(temp, 4096);
	if (!serverworks_private.registers) {
		printk (KERN_ERR PFX "Unable to ioremap() memory.\n");
		return -ENOMEM;
	}

	writeb(0xA, serverworks_private.registers+SVWRKS_GART_CACHE);
	readb(serverworks_private.registers+SVWRKS_GART_CACHE);	/* PCI Posting. */

	writel(agp_bridge->gatt_bus_addr, serverworks_private.registers+SVWRKS_GATTBASE);
	readl(serverworks_private.registers+SVWRKS_GATTBASE);	/* PCI Posting. */

	cap_reg = readw(serverworks_private.registers+SVWRKS_COMMAND);
	cap_reg &= ~0x0007;
	cap_reg |= 0x4;
	writew(cap_reg, serverworks_private.registers+SVWRKS_COMMAND);
	readw(serverworks_private.registers+SVWRKS_COMMAND);

	pci_read_config_byte(serverworks_private.svrwrks_dev,SVWRKS_AGP_ENABLE, &enable_reg);
	enable_reg |= 0x1; /* Agp Enable bit */
	pci_write_config_byte(serverworks_private.svrwrks_dev,SVWRKS_AGP_ENABLE, enable_reg);
	serverworks_tlbflush(NULL);

	agp_bridge->capndx = pci_find_capability(serverworks_private.svrwrks_dev, PCI_CAP_ID_AGP);

	/* Fill in the mode register */
	pci_read_config_dword(serverworks_private.svrwrks_dev,
			      agp_bridge->capndx+PCI_AGP_STATUS, &agp_bridge->mode);

	pci_read_config_byte(agp_bridge->dev, SVWRKS_CACHING, &enable_reg);
	enable_reg &= ~0x3;
	pci_write_config_byte(agp_bridge->dev, SVWRKS_CACHING, enable_reg);

	pci_read_config_byte(agp_bridge->dev, SVWRKS_FEATURE, &enable_reg);
	enable_reg |= (1<<6);
	pci_write_config_byte(agp_bridge->dev,SVWRKS_FEATURE, enable_reg);

	return 0;
}

static void serverworks_cleanup(void)
{
	iounmap((void __iomem *) serverworks_private.registers);
}

static int serverworks_insert_memory(struct agp_memory *mem,
			     off_t pg_start, int type)
{
	int i, j, num_entries;
	unsigned long __iomem *cur_gatt;
	unsigned long addr;

	num_entries = A_SIZE_LVL2(agp_bridge->current_size)->num_entries;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}
	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}

	j = pg_start;
	while (j < (pg_start + mem->page_count)) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = SVRWRKS_GET_GATT(addr);
		if (!PGE_EMPTY(agp_bridge, readl(cur_gatt+GET_GATT_OFF(addr))))
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = SVRWRKS_GET_GATT(addr);
		writel(agp_bridge->driver->mask_memory(agp_bridge, mem->memory[i], mem->type), cur_gatt+GET_GATT_OFF(addr));
	}
	serverworks_tlbflush(mem);
	return 0;
}

static int serverworks_remove_memory(struct agp_memory *mem, off_t pg_start,
			     int type)
{
	int i;
	unsigned long __iomem *cur_gatt;
	unsigned long addr;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	global_cache_flush();
	serverworks_tlbflush(mem);

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		addr = (i * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = SVRWRKS_GET_GATT(addr);
		writel(agp_bridge->scratch_page, cur_gatt+GET_GATT_OFF(addr));
	}

	serverworks_tlbflush(mem);
	return 0;
}

static struct gatt_mask serverworks_masks[] =
{
	{.mask = 1, .type = 0}
};

static struct aper_size_info_lvl2 serverworks_sizes[7] =
{
	{2048, 524288, 0x80000000},
	{1024, 262144, 0xc0000000},
	{512, 131072, 0xe0000000},
	{256, 65536, 0xf0000000},
	{128, 32768, 0xf8000000},
	{64, 16384, 0xfc000000},
	{32, 8192, 0xfe000000}
};

static void serverworks_agp_enable(struct agp_bridge_data *bridge, u32 mode)
{
	u32 command;

	pci_read_config_dword(serverworks_private.svrwrks_dev,
			      bridge->capndx + PCI_AGP_STATUS,
			      &command);

	command = agp_collect_device_status(bridge, mode, command);

	command &= ~0x10;	/* disable FW */
	command &= ~0x08;

	command |= 0x100;

	pci_write_config_dword(serverworks_private.svrwrks_dev,
			       bridge->capndx + PCI_AGP_COMMAND,
			       command);

	agp_device_command(command, 0);
}

static struct agp_bridge_driver sworks_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= serverworks_sizes,
	.size_type		= LVL2_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= serverworks_configure,
	.fetch_size		= serverworks_fetch_size,
	.cleanup		= serverworks_cleanup,
	.tlb_flush		= serverworks_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= serverworks_masks,
	.agp_enable		= serverworks_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= serverworks_create_gatt_table,
	.free_gatt_table	= serverworks_free_gatt_table,
	.insert_memory		= serverworks_insert_memory,
	.remove_memory		= serverworks_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
};

static int __devinit agp_serverworks_probe(struct pci_dev *pdev,
					   const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	struct pci_dev *bridge_dev;
	u32 temp, temp2;
	u8 cap_ptr = 0;

	/* Everything is on func 1 here so we are hardcoding function one */
	bridge_dev = pci_find_slot((unsigned int)pdev->bus->number,
			PCI_DEVFN(0, 1));
	if (!bridge_dev) {
		printk(KERN_INFO PFX "Detected a Serverworks chipset "
		       "but could not find the secondary device.\n");
		return -ENODEV;
	}

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);

	switch (pdev->device) {
	case 0x0006:
		printk (KERN_ERR PFX "ServerWorks CNB20HE is unsupported due to lack of documentation.\n");
		return -ENODEV;

	case PCI_DEVICE_ID_SERVERWORKS_HE:
	case PCI_DEVICE_ID_SERVERWORKS_LE:
	case 0x0007:
		break;

	default:
		if (cap_ptr)
			printk(KERN_ERR PFX "Unsupported Serverworks chipset "
					"(device id: %04x)\n", pdev->device);
		return -ENODEV;
	}

	serverworks_private.svrwrks_dev = bridge_dev;
	serverworks_private.gart_addr_ofs = 0x10;

	pci_read_config_dword(pdev, SVWRKS_APSIZE, &temp);
	if (temp & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		pci_read_config_dword(pdev, SVWRKS_APSIZE + 4, &temp2);
		if (temp2 != 0) {
			printk(KERN_INFO PFX "Detected 64 bit aperture address, "
			       "but top bits are not zero.  Disabling agp\n");
			return -ENODEV;
		}
		serverworks_private.mm_addr_ofs = 0x18;
	} else
		serverworks_private.mm_addr_ofs = 0x14;

	pci_read_config_dword(pdev, serverworks_private.mm_addr_ofs, &temp);
	if (temp & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		pci_read_config_dword(pdev,
				serverworks_private.mm_addr_ofs + 4, &temp2);
		if (temp2 != 0) {
			printk(KERN_INFO PFX "Detected 64 bit MMIO address, "
			       "but top bits are not zero.  Disabling agp\n");
			return -ENODEV;
		}
	}

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->driver = &sworks_driver;
	bridge->dev_private_data = &serverworks_private,
	bridge->dev = pdev;

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_serverworks_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_serverworks_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_SERVERWORKS,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_serverworks_pci_table);

static struct pci_driver agp_serverworks_pci_driver = {
	.name		= "agpgart-serverworks",
	.id_table	= agp_serverworks_pci_table,
	.probe		= agp_serverworks_probe,
	.remove		= agp_serverworks_remove,
};

static int __init agp_serverworks_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_serverworks_pci_driver);
}

static void __exit agp_serverworks_cleanup(void)
{
	pci_unregister_driver(&agp_serverworks_pci_driver);
}

module_init(agp_serverworks_init);
module_exit(agp_serverworks_cleanup);

MODULE_LICENSE("GPL and additional rights");

