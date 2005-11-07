/*
 * VIA AGPGART routines.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

static struct pci_device_id agp_via_pci_table[];

#define VIA_GARTCTRL	0x80
#define VIA_APSIZE	0x84
#define VIA_ATTBASE	0x88

#define VIA_AGP3_GARTCTRL	0x90
#define VIA_AGP3_APSIZE		0x94
#define VIA_AGP3_ATTBASE	0x98
#define VIA_AGPSEL		0xfd

static int via_fetch_size(void)
{
	int i;
	u8 temp;
	struct aper_size_info_8 *values;

	values = A_SIZE_8(agp_bridge->driver->aperture_sizes);
	pci_read_config_byte(agp_bridge->dev, VIA_APSIZE, &temp);
	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}
	printk(KERN_ERR PFX "Unknown aperture size from AGP bridge (0x%x)\n", temp);
	return 0;
}


static int via_configure(void)
{
	u32 temp;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);
	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE,
			      current_size->size_value);
	/* address to map too */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* GART control register */
	pci_write_config_dword(agp_bridge->dev, VIA_GARTCTRL, 0x0000000f);

	/* attbase - aperture GATT base */
	pci_write_config_dword(agp_bridge->dev, VIA_ATTBASE,
			    (agp_bridge->gatt_bus_addr & 0xfffff000) | 3);
	return 0;
}


static void via_cleanup(void)
{
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE,
			      previous_size->size_value);
	/* Do not disable by writing 0 to VIA_ATTBASE, it screws things up
	 * during reinitialization.
	 */
}


static void via_tlbflush(struct agp_memory *mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge->dev, VIA_GARTCTRL, &temp);
	temp |= (1<<7);
	pci_write_config_dword(agp_bridge->dev, VIA_GARTCTRL, temp);
	temp &= ~(1<<7);
	pci_write_config_dword(agp_bridge->dev, VIA_GARTCTRL, temp);
}


static struct aper_size_info_8 via_generic_sizes[9] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 128},
	{64, 16384, 4, 192},
	{32, 8192, 3, 224},
	{16, 4096, 2, 240},
	{8, 2048, 1, 248},
	{4, 1024, 0, 252},
	{2, 512, 0, 254},
	{1, 256, 0, 255}
};


static int via_fetch_size_agp3(void)
{
	int i;
	u16 temp;
	struct aper_size_info_16 *values;

	values = A_SIZE_16(agp_bridge->driver->aperture_sizes);
	pci_read_config_word(agp_bridge->dev, VIA_AGP3_APSIZE, &temp);
	temp &= 0xfff;

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


static int via_configure_agp3(void)
{
	u32 temp;
	struct aper_size_info_16 *current_size;

	current_size = A_SIZE_16(agp_bridge->current_size);

	/* address to map too */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture GATT base */
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_ATTBASE,
		agp_bridge->gatt_bus_addr & 0xfffff000);

	/* 1. Enable GTLB in RX90<7>, all AGP aperture access needs to fetch
	 *    translation table first.
	 * 2. Enable AGP aperture in RX91<0>. This bit controls the enabling of the
	 *    graphics AGP aperture for the AGP3.0 port.
	 */
	pci_read_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, temp | (3<<7));
	return 0;
}


static void via_cleanup_agp3(void)
{
	struct aper_size_info_16 *previous_size;

	previous_size = A_SIZE_16(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE, previous_size->size_value);
}


static void via_tlbflush_agp3(struct agp_memory *mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, temp & ~(1<<7));
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, temp);
}


static struct agp_bridge_driver via_agp3_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= agp3_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 10,
	.configure		= via_configure_agp3,
	.fetch_size		= via_fetch_size_agp3,
	.cleanup		= via_cleanup_agp3,
	.tlb_flush		= via_tlbflush_agp3,
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
	.agp_destroy_page	= agp_generic_destroy_page,
};

static struct agp_bridge_driver via_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= via_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 9,
	.configure		= via_configure,
	.fetch_size		= via_fetch_size,
	.cleanup		= via_cleanup,
	.tlb_flush		= via_tlbflush,
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
	.agp_destroy_page	= agp_generic_destroy_page,
};

static struct agp_device_ids via_agp_device_ids[] __devinitdata =
{
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C597_0,
		.chipset_name	= "Apollo VP3",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_82C598_0,
		.chipset_name	= "Apollo MVP3",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_8501_0,
		.chipset_name	= "Apollo MVP4",
	},

	/* VT8601 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8601_0,
		.chipset_name	= "Apollo ProMedia/PLE133Ta",
	},

	/* VT82C693A / VT28C694T */
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C691_0,
		.chipset_name	= "Apollo Pro 133",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_8371_0,
		.chipset_name	= "KX133",
	},

	/* VT8633 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8633_0,
		.chipset_name	= "Pro 266",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_XN266,
		.chipset_name	= "Apollo Pro266",
	},

	/* VT8361 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8361,
		.chipset_name	= "KLE133",
	},

	/* VT8365 / VT8362 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8363_0,
		.chipset_name	= "Twister-K/KT133x/KM133",
	},

	/* VT8753A */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8753_0,
		.chipset_name	= "P4X266",
	},

	/* VT8366 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8367_0,
		.chipset_name	= "KT266/KY266x/KT333",
	},

	/* VT8633 (for CuMine/ Celeron) */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8653_0,
		.chipset_name	= "Pro266T",
	},

	/* KM266 / PM266 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_XM266,
		.chipset_name	= "PM266/KM266",
	},

	/* CLE266 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_862X_0,
		.chipset_name	= "CLE266",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_8377_0,
		.chipset_name	= "KT400/KT400A/KT600",
	},

	/* VT8604 / VT8605 / VT8603
	 * (Apollo Pro133A chipset with S3 Savage4) */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8605_0,
		.chipset_name	= "ProSavage PM133/PL133/PN133"
	},

	/* P4M266x/P4N266 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8703_51_0,
		.chipset_name	= "P4M266x/P4N266",
	},

	/* VT8754 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8754C_0,
		.chipset_name	= "PT800",
	},

	/* P4X600 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8763_0,
		.chipset_name	= "P4X600"
	},

	/* KM400 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8378_0,
		.chipset_name	= "KM400/KM400A",
	},

	/* PT880 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_PT880,
		.chipset_name	= "PT880",
	},

	/* PT890 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8783_0,
		.chipset_name	= "PT890",
	},

	/* PM800/PN800/PM880/PN880 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_PX8X0_0,
		.chipset_name	= "PM800/PN800/PM880/PN880",
	},
	/* KT880 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_3269_0,
		.chipset_name	= "KT880",
	},
	/* KTxxx/Px8xx */
	{
		.device_id	= PCI_DEVICE_ID_VIA_83_87XX_1,
		.chipset_name	= "VT83xx/VT87xx/KTxxx/Px8xx",
	},
	/* P4M800 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_3296_0,
		.chipset_name	= "P4M800",
	},

	{ }, /* dummy final entry, always present */
};


/*
 * VIA's AGP3 chipsets do magick to put the AGP bridge compliant
 * with the same standards version as the graphics card.
 */
static void check_via_agp3 (struct agp_bridge_data *bridge)
{
	u8 reg;

	pci_read_config_byte(bridge->dev, VIA_AGPSEL, &reg);
	/* Check AGP 2.0 compatibility mode. */
	if ((reg & (1<<1))==0)
		bridge->driver = &via_agp3_driver;
}


static int __devinit agp_via_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = via_agp_device_ids;
	struct agp_bridge_data *bridge;
	int j = 0;
	u8 cap_ptr;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	j = ent - agp_via_pci_table;
	printk (KERN_INFO PFX "Detected VIA %s chipset\n", devs[j].chipset_name);

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->dev = pdev;
	bridge->capndx = cap_ptr;
	bridge->driver = &via_driver;

	/*
	 * Garg, there are KT400s with KT266 IDs.
	 */
	if (pdev->device == PCI_DEVICE_ID_VIA_8367_0) {
		/* Is there a KT400 subsystem ? */
		if (pdev->subsystem_device == PCI_DEVICE_ID_VIA_8377_0) {
			printk(KERN_INFO PFX "Found KT400 in disguise as a KT266.\n");
			check_via_agp3(bridge);
		}
	}

	/* If this is an AGP3 bridge, check which mode its in and adjust. */
	get_agp_version(bridge);
	if (bridge->major_version >= 3)
		check_via_agp3(bridge);

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			bridge->capndx+PCI_AGP_STATUS, &bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_via_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

#ifdef CONFIG_PM

static int agp_via_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state (pdev);
	pci_set_power_state (pdev, PCI_D3hot);

	return 0;
}

static int agp_via_resume(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	pci_set_power_state (pdev, PCI_D0);
	pci_restore_state(pdev);

	if (bridge->driver == &via_agp3_driver)
		return via_configure_agp3();
	else if (bridge->driver == &via_driver)
		return via_configure();

	return 0;
}

#endif /* CONFIG_PM */

/* must be the same order as name table above */
static struct pci_device_id agp_via_pci_table[] = {
#define ID(x) \
	{						\
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),	\
	.class_mask	= ~0,				\
	.vendor		= PCI_VENDOR_ID_VIA,		\
	.device		= x,				\
	.subvendor	= PCI_ANY_ID,			\
	.subdevice	= PCI_ANY_ID,			\
	}
	ID(PCI_DEVICE_ID_VIA_82C597_0),
	ID(PCI_DEVICE_ID_VIA_82C598_0),
	ID(PCI_DEVICE_ID_VIA_8501_0),
	ID(PCI_DEVICE_ID_VIA_8601_0),
	ID(PCI_DEVICE_ID_VIA_82C691_0),
	ID(PCI_DEVICE_ID_VIA_8371_0),
	ID(PCI_DEVICE_ID_VIA_8633_0),
	ID(PCI_DEVICE_ID_VIA_XN266),
	ID(PCI_DEVICE_ID_VIA_8361),
	ID(PCI_DEVICE_ID_VIA_8363_0),
	ID(PCI_DEVICE_ID_VIA_8753_0),
	ID(PCI_DEVICE_ID_VIA_8367_0),
	ID(PCI_DEVICE_ID_VIA_8653_0),
	ID(PCI_DEVICE_ID_VIA_XM266),
	ID(PCI_DEVICE_ID_VIA_862X_0),
	ID(PCI_DEVICE_ID_VIA_8377_0),
	ID(PCI_DEVICE_ID_VIA_8605_0),
	ID(PCI_DEVICE_ID_VIA_8703_51_0),
	ID(PCI_DEVICE_ID_VIA_8754C_0),
	ID(PCI_DEVICE_ID_VIA_8763_0),
	ID(PCI_DEVICE_ID_VIA_8378_0),
	ID(PCI_DEVICE_ID_VIA_PT880),
	ID(PCI_DEVICE_ID_VIA_8783_0),
	ID(PCI_DEVICE_ID_VIA_PX8X0_0),
	ID(PCI_DEVICE_ID_VIA_3269_0),
	ID(PCI_DEVICE_ID_VIA_83_87XX_1),
	ID(PCI_DEVICE_ID_VIA_3296_0),
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_via_pci_table);


static struct pci_driver agp_via_pci_driver = {
	.owner		= THIS_MODULE,
	.name		= "agpgart-via",
	.id_table	= agp_via_pci_table,
	.probe		= agp_via_probe,
	.remove		= agp_via_remove,
#ifdef CONFIG_PM
	.suspend	= agp_via_suspend,
	.resume		= agp_via_resume,
#endif
};


static int __init agp_via_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_via_pci_driver);
}

static void __exit agp_via_cleanup(void)
{
	pci_unregister_driver(&agp_via_pci_driver);
}

module_init(agp_via_init);
module_exit(agp_via_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
