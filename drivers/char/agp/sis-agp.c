/*
 * SiS AGPGART routines.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <linux/delay.h>
#include "agp.h"

#define SIS_ATTBASE	0x90
#define SIS_APSIZE	0x94
#define SIS_TLBCNTRL	0x97
#define SIS_TLBFLUSH	0x98

static int __devinitdata agp_sis_force_delay = 0;
static int __devinitdata agp_sis_agp_spec = -1;

static int sis_fetch_size(void)
{
	u8 temp_size;
	int i;
	struct aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge->dev, SIS_APSIZE, &temp_size);
	values = A_SIZE_8(agp_bridge->driver->aperture_sizes);
	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if ((temp_size == values[i].size_value) ||
		    ((temp_size & ~(0x03)) ==
		     (values[i].size_value & ~(0x03)))) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void sis_tlbflush(struct agp_memory *mem)
{
	pci_write_config_byte(agp_bridge->dev, SIS_TLBFLUSH, 0x02);
}

static int sis_configure(void)
{
	u32 temp;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);
	pci_write_config_byte(agp_bridge->dev, SIS_TLBCNTRL, 0x05);
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	pci_write_config_dword(agp_bridge->dev, SIS_ATTBASE,
			       agp_bridge->gatt_bus_addr);
	pci_write_config_byte(agp_bridge->dev, SIS_APSIZE,
			      current_size->size_value);
	return 0;
}

static void sis_cleanup(void)
{
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, SIS_APSIZE,
			      (previous_size->size_value & ~(0x03)));
}

static void sis_delayed_enable(struct agp_bridge_data *bridge, u32 mode)
{
	struct pci_dev *device = NULL;
	u32 command;
	int rate;

	printk(KERN_INFO PFX "Found an AGP %d.%d compliant device at %s.\n",
		agp_bridge->major_version,
		agp_bridge->minor_version,
		pci_name(agp_bridge->dev));

	pci_read_config_dword(agp_bridge->dev, agp_bridge->capndx + PCI_AGP_STATUS, &command);
	command = agp_collect_device_status(bridge, mode, command);
	command |= AGPSTAT_AGP_ENABLE;
	rate = (command & 0x7) << 2;

	for_each_pci_dev(device) {
		u8 agp = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (!agp)
			continue;

		printk(KERN_INFO PFX "Putting AGP V3 device at %s into %dx mode\n",
			pci_name(device), rate);

		pci_write_config_dword(device, agp + PCI_AGP_COMMAND, command);

		/*
		 * Weird: on some sis chipsets any rate change in the target
		 * command register triggers a 5ms screwup during which the master
		 * cannot be configured
		 */
		if (device->device == bridge->dev->device) {
			printk(KERN_INFO PFX "SiS delay workaround: giving bridge time to recover.\n");
			msleep(10);
		}
	}
}

static struct aper_size_info_8 sis_generic_sizes[7] =
{
	{256, 65536, 6, 99},
	{128, 32768, 5, 83},
	{64, 16384, 4, 67},
	{32, 8192, 3, 51},
	{16, 4096, 2, 35},
	{8, 2048, 1, 19},
	{4, 1024, 0, 3}
};

static struct agp_bridge_driver sis_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes 	= sis_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= sis_configure,
	.fetch_size		= sis_fetch_size,
	.cleanup		= sis_cleanup,
	.tlb_flush		= sis_tlbflush,
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

static struct agp_device_ids sis_agp_device_ids[] __devinitdata =
{
	{
		.device_id	= PCI_DEVICE_ID_SI_5591_AGP,
		.chipset_name	= "5591",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_530,
		.chipset_name	= "530",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_540,
		.chipset_name	= "540",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_550,
		.chipset_name	= "550",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_620,
		.chipset_name	= "620",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_630,
		.chipset_name	= "630",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_635,
		.chipset_name	= "635",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_645,
		.chipset_name	= "645",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_646,
		.chipset_name	= "646",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_648,
		.chipset_name	= "648",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_650,
		.chipset_name	= "650",
	},
	{
		.device_id  = PCI_DEVICE_ID_SI_651,
		.chipset_name   = "651",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_655,
		.chipset_name	= "655",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_661,
		.chipset_name	= "661",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_730,
		.chipset_name	= "730",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_735,
		.chipset_name	= "735",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_740,
		.chipset_name	= "740",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_741,
		.chipset_name	= "741",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_745,
		.chipset_name	= "745",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_746,
		.chipset_name	= "746",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_760,
		.chipset_name	= "760",
	},
	{ }, /* dummy final entry, always present */
};


// chipsets that require the 'delay hack'
static int sis_broken_chipsets[] __devinitdata = {
	PCI_DEVICE_ID_SI_648,
	PCI_DEVICE_ID_SI_746,
	0 // terminator
};

static void __devinit sis_get_driver(struct agp_bridge_data *bridge)
{
	int i;

	for(i=0; sis_broken_chipsets[i]!=0; ++i)
		if(bridge->dev->device==sis_broken_chipsets[i])
			break;

	if(sis_broken_chipsets[i] || agp_sis_force_delay)
		sis_driver.agp_enable=sis_delayed_enable;

	// sis chipsets that indicate less than agp3.5
	// are not actually fully agp3 compliant
	if ((agp_bridge->major_version == 3 && agp_bridge->minor_version >= 5
	     && agp_sis_agp_spec!=0) || agp_sis_agp_spec==1) {
		sis_driver.aperture_sizes = agp3_generic_sizes;
		sis_driver.size_type = U16_APER_SIZE;
		sis_driver.num_aperture_sizes = AGP_GENERIC_SIZES_ENTRIES;
		sis_driver.configure = agp3_generic_configure;
		sis_driver.fetch_size = agp3_generic_fetch_size;
		sis_driver.cleanup = agp3_generic_cleanup;
		sis_driver.tlb_flush = agp3_generic_tlbflush;
	}
}


static int __devinit agp_sis_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = sis_agp_device_ids;
	struct agp_bridge_data *bridge;
	u8 cap_ptr;
	int j;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	/* probe for known chipsets */
	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id) {
			printk(KERN_INFO PFX "Detected SiS %s chipset\n",
					devs[j].chipset_name);
			goto found;
		}
	}

	printk(KERN_ERR PFX "Unsupported SiS chipset (device id: %04x)\n",
		    pdev->device);
	return -ENODEV;

found:
	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->driver = &sis_driver;
	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	get_agp_version(bridge);

	/* Fill in the mode register */
	pci_read_config_dword(pdev, bridge->capndx+PCI_AGP_STATUS, &bridge->mode);
	sis_get_driver(bridge);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_sis_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_sis_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_SI,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_sis_pci_table);

static struct pci_driver agp_sis_pci_driver = {
	.name		= "agpgart-sis",
	.id_table	= agp_sis_pci_table,
	.probe		= agp_sis_probe,
	.remove		= agp_sis_remove,
};

static int __init agp_sis_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_sis_pci_driver);
}

static void __exit agp_sis_cleanup(void)
{
	pci_unregister_driver(&agp_sis_pci_driver);
}

module_init(agp_sis_init);
module_exit(agp_sis_cleanup);

module_param(agp_sis_force_delay, bool, 0);
MODULE_PARM_DESC(agp_sis_force_delay,"forces sis delay hack");
module_param(agp_sis_agp_spec, int, 0);
MODULE_PARM_DESC(agp_sis_agp_spec,"0=force sis init, 1=force generic agp3 init, default: autodetect");
MODULE_LICENSE("GPL and additional rights");
