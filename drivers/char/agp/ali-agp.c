/*
 * ALi AGPGART routines.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <asm/page.h>		/* PAGE_SIZE */
#include "agp.h"

#define ALI_AGPCTRL	0xb8
#define ALI_ATTBASE	0xbc
#define ALI_TLBCTRL	0xc0
#define ALI_TAGCTRL	0xc4
#define ALI_CACHE_FLUSH_CTRL	0xD0
#define ALI_CACHE_FLUSH_ADDR_MASK	0xFFFFF000
#define ALI_CACHE_FLUSH_EN	0x100

static int ali_fetch_size(void)
{
	int i;
	u32 temp;
	struct aper_size_info_32 *values;

	pci_read_config_dword(agp_bridge->dev, ALI_ATTBASE, &temp);
	temp &= ~(0xfffffff0);
	values = A_SIZE_32(agp_bridge->driver->aperture_sizes);

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

static void ali_tlbflush(struct agp_memory *mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
	temp &= 0xfffffff0;
	temp |= (1<<0 | 1<<1);
	pci_write_config_dword(agp_bridge->dev, ALI_TAGCTRL, temp);
}

static void ali_cleanup(void)
{
	struct aper_size_info_32 *previous_size;
	u32 temp;

	previous_size = A_SIZE_32(agp_bridge->previous_size);

	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
// clear tag
	pci_write_config_dword(agp_bridge->dev, ALI_TAGCTRL,
			((temp & 0xffffff00) | 0x00000001|0x00000002));

	pci_read_config_dword(agp_bridge->dev,  ALI_ATTBASE, &temp);
	pci_write_config_dword(agp_bridge->dev, ALI_ATTBASE,
			((temp & 0x00000ff0) | previous_size->size_value));
}

static int ali_configure(void)
{
	u32 temp;
	struct aper_size_info_32 *current_size;

	current_size = A_SIZE_32(agp_bridge->current_size);

	/* aperture size and gatt addr */
	pci_read_config_dword(agp_bridge->dev, ALI_ATTBASE, &temp);
	temp = (((temp & 0x00000ff0) | (agp_bridge->gatt_bus_addr & 0xfffff000))
			| (current_size->size_value & 0xf));
	pci_write_config_dword(agp_bridge->dev, ALI_ATTBASE, temp);

	/* tlb control */
	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, ALI_TLBCTRL, ((temp & 0xffffff00) | 0x00000010));

	/* address to map to */
	agp_bridge->gart_bus_addr = pci_bus_address(agp_bridge->dev,
						    AGP_APERTURE_BAR);

#if 0
	if (agp_bridge->type == ALI_M1541) {
		u32 nlvm_addr = 0;

		switch (current_size->size_value) {
			case 0:  break;
			case 1:  nlvm_addr = 0x100000;break;
			case 2:  nlvm_addr = 0x200000;break;
			case 3:  nlvm_addr = 0x400000;break;
			case 4:  nlvm_addr = 0x800000;break;
			case 6:  nlvm_addr = 0x1000000;break;
			case 7:  nlvm_addr = 0x2000000;break;
			case 8:  nlvm_addr = 0x4000000;break;
			case 9:  nlvm_addr = 0x8000000;break;
			case 10: nlvm_addr = 0x10000000;break;
			default: break;
		}
		nlvm_addr--;
		nlvm_addr&=0xfff00000;

		nlvm_addr+= agp_bridge->gart_bus_addr;
		nlvm_addr|=(agp_bridge->gart_bus_addr>>12);
		dev_info(&agp_bridge->dev->dev, "nlvm top &base = %8x\n",
			 nlvm_addr);
	}
#endif

	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
	temp &= 0xffffff7f;		//enable TLB
	pci_write_config_dword(agp_bridge->dev, ALI_TLBCTRL, temp);

	return 0;
}


static void m1541_cache_flush(void)
{
	int i, page_count;
	u32 temp;

	global_cache_flush();

	page_count = 1 << A_SIZE_32(agp_bridge->current_size)->page_order;
	for (i = 0; i < PAGE_SIZE * page_count; i += PAGE_SIZE) {
		pci_read_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL,
				&temp);
		pci_write_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL,
				(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
				  (agp_bridge->gatt_bus_addr + i)) |
				 ALI_CACHE_FLUSH_EN));
	}
}

static struct page *m1541_alloc_page(struct agp_bridge_data *bridge)
{
	struct page *page = agp_generic_alloc_page(agp_bridge);
	u32 temp;

	if (!page)
		return NULL;

	pci_read_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL,
			(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
			  page_to_phys(page)) | ALI_CACHE_FLUSH_EN ));
	return page;
}

static void ali_destroy_page(struct page *page, int flags)
{
	if (page) {
		if (flags & AGP_PAGE_DESTROY_UNMAP) {
			global_cache_flush();	/* is this really needed?  --hch */
			agp_generic_destroy_page(page, flags);
		} else
			agp_generic_destroy_page(page, flags);
	}
}

static void m1541_destroy_page(struct page *page, int flags)
{
	u32 temp;

	if (page == NULL)
		return;

	if (flags & AGP_PAGE_DESTROY_UNMAP) {
		global_cache_flush();

		pci_read_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL, &temp);
		pci_write_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL,
				       (((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
					 page_to_phys(page)) | ALI_CACHE_FLUSH_EN));
	}
	agp_generic_destroy_page(page, flags);
}


/* Setup function */

static const struct aper_size_info_32 ali_generic_sizes[7] =
{
	{256, 65536, 6, 10},
	{128, 32768, 5, 9},
	{64, 16384, 4, 8},
	{32, 8192, 3, 7},
	{16, 4096, 2, 6},
	{8, 2048, 1, 4},
	{4, 1024, 0, 3}
};

static const struct agp_bridge_driver ali_generic_bridge = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= ali_generic_sizes,
	.size_type		= U32_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= ali_configure,
	.fetch_size		= ali_fetch_size,
	.cleanup		= ali_cleanup,
	.tlb_flush		= ali_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= NULL,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= ali_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver ali_m1541_bridge = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= ali_generic_sizes,
	.size_type		= U32_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= ali_configure,
	.fetch_size		= ali_fetch_size,
	.cleanup		= ali_cleanup,
	.tlb_flush		= ali_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= NULL,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= m1541_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= m1541_alloc_page,
	.agp_destroy_page	= m1541_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};


static struct agp_device_ids ali_agp_device_ids[] =
{
	{
		.device_id	= PCI_DEVICE_ID_AL_M1541,
		.chipset_name	= "M1541",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1621,
		.chipset_name	= "M1621",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1631,
		.chipset_name	= "M1631",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1632,
		.chipset_name	= "M1632",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1641,
		.chipset_name	= "M1641",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1644,
		.chipset_name	= "M1644",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1647,
		.chipset_name	= "M1647",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1651,
		.chipset_name	= "M1651",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1671,
		.chipset_name	= "M1671",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1681,
		.chipset_name	= "M1681",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1683,
		.chipset_name	= "M1683",
	},

	{ }, /* dummy final entry, always present */
};

static int agp_ali_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = ali_agp_device_ids;
	struct agp_bridge_data *bridge;
	u8 hidden_1621_id, cap_ptr;
	int j;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	/* probe for known chipsets */
	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id)
			goto found;
	}

	dev_err(&pdev->dev, "unsupported ALi chipset [%04x/%04x])\n",
		pdev->vendor, pdev->device);
	return -ENODEV;


found:
	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	switch (pdev->device) {
	case PCI_DEVICE_ID_AL_M1541:
		bridge->driver = &ali_m1541_bridge;
		break;
	case PCI_DEVICE_ID_AL_M1621:
		pci_read_config_byte(pdev, 0xFB, &hidden_1621_id);
		switch (hidden_1621_id) {
		case 0x31:
			devs[j].chipset_name = "M1631";
			break;
		case 0x32:
			devs[j].chipset_name = "M1632";
			break;
		case 0x41:
			devs[j].chipset_name = "M1641";
			break;
		case 0x43:
			devs[j].chipset_name = "M1621";
			break;
		case 0x47:
			devs[j].chipset_name = "M1647";
			break;
		case 0x51:
			devs[j].chipset_name = "M1651";
			break;
		default:
			break;
		}
		/*FALLTHROUGH*/
	default:
		bridge->driver = &ali_generic_bridge;
	}

	dev_info(&pdev->dev, "ALi %s chipset\n", devs[j].chipset_name);

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			bridge->capndx+PCI_AGP_STATUS,
			&bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void agp_ali_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_ali_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AL,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_ali_pci_table);

static struct pci_driver agp_ali_pci_driver = {
	.name		= "agpgart-ali",
	.id_table	= agp_ali_pci_table,
	.probe		= agp_ali_probe,
	.remove		= agp_ali_remove,
};

static int __init agp_ali_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_ali_pci_driver);
}

static void __exit agp_ali_cleanup(void)
{
	pci_unregister_driver(&agp_ali_pci_driver);
}

module_init(agp_ali_init);
module_exit(agp_ali_cleanup);

MODULE_AUTHOR("Dave Jones");
MODULE_LICENSE("GPL and additional rights");

