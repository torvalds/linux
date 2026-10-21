// SPDX-License-Identifier: GPL-2.0-only
/*
 * VIA AGPGART routines.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

static const struct pci_device_id agp_via_pci_table[];

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
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);
	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE,
			      current_size->size_value);
	/* address to map to */
	agp_bridge->gart_bus_addr = pci_bus_address(agp_bridge->dev,
						    AGP_APERTURE_BAR);

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


static const struct aper_size_info_8 via_generic_sizes[9] =
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

	/* address to map to */
	agp_bridge->gart_bus_addr = pci_bus_address(agp_bridge->dev,
						    AGP_APERTURE_BAR);

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


static const struct agp_bridge_driver via_agp3_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= agp3_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 10,
	.needs_scratch_page	= true,
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
	.agp_alloc_pages	= agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages	= agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver via_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= via_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 9,
	.needs_scratch_page	= true,
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
	.agp_alloc_pages	= agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages	= agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
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


static int agp_via_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	u8 cap_ptr;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	dev_info(&pdev->dev, "Detected VIA %s chipset\n", (const char *)ent->driver_data);

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

static void agp_via_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static int agp_via_resume(struct device *dev)
{
	struct agp_bridge_data *bridge = dev_get_drvdata(dev);

	if (bridge->driver == &via_agp3_driver)
		return via_configure_agp3();
	else if (bridge->driver == &via_driver)
		return via_configure();

	return 0;
}

static const struct pci_device_id agp_via_pci_table[] = {
#define ID(x, name) \
	{						\
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),	\
	.class_mask	= ~0,				\
	.vendor		= PCI_VENDOR_ID_VIA,		\
	.device		= x,				\
	.subvendor	= PCI_ANY_ID,			\
	.subdevice	= PCI_ANY_ID,			\
	.driver_data	= (kernel_ulong_t)name,		\
	}
	ID(PCI_DEVICE_ID_VIA_82C597_0, "Apollo VP3"),
	ID(PCI_DEVICE_ID_VIA_82C598_0, "Apollo MVP3"),
	ID(PCI_DEVICE_ID_VIA_8501_0, "Apollo MVP4"),
	/* VT8601 */
	ID(PCI_DEVICE_ID_VIA_8601_0, "Apollo ProMedia/PLE133Ta"),
	/* VT82C693A / VT28C694T */
	ID(PCI_DEVICE_ID_VIA_82C691_0, "Apollo Pro 133"),
	ID(PCI_DEVICE_ID_VIA_8371_0, "KX133"),
	/* VT8633 */
	ID(PCI_DEVICE_ID_VIA_8633_0, "Pro 266"),
	ID(PCI_DEVICE_ID_VIA_XN266, "Apollo Pro266"),
	/* VT8361 */
	ID(PCI_DEVICE_ID_VIA_8361, "KLE133"),
	/* VT8365 / VT8362 */
	ID(PCI_DEVICE_ID_VIA_8363_0, "Twister-K/KT133x/KM133"),
	/* VT8753A */
	ID(PCI_DEVICE_ID_VIA_8753_0, "P4X266"),
	/* VT8366 */
	ID(PCI_DEVICE_ID_VIA_8367_0, "KT266/KY266x/KT333"),
	/* VT8633 (for CuMine/ Celeron) */
	ID(PCI_DEVICE_ID_VIA_8653_0, "Pro266T"),
	/* KM266 / PM266 */
	ID(PCI_DEVICE_ID_VIA_XM266, "PM266/KM266"),
	/* CLE266 */
	ID(PCI_DEVICE_ID_VIA_862X_0, "CLE266"),
	ID(PCI_DEVICE_ID_VIA_8377_0, "KT400/KT400A/KT600"),
	/* VT8604 / VT8605 / VT8603 (Apollo Pro133A chipset with S3 Savage4) */
	ID(PCI_DEVICE_ID_VIA_8605_0, "ProSavage PM133/PL133/PN133"),
	/* P4M266x/P4N266 */
	ID(PCI_DEVICE_ID_VIA_8703_51_0, "P4M266x/P4N266"),
	/* VT8754 */
	ID(PCI_DEVICE_ID_VIA_8754C_0, "PT800"),
	/* P4X600 */
	ID(PCI_DEVICE_ID_VIA_8763_0, "P4X600"),
	/* KM400 */
	ID(PCI_DEVICE_ID_VIA_8378_0, "KM400/KM400A"),
	/* PT880 */
	ID(PCI_DEVICE_ID_VIA_PT880, "PT880"),
	/* PT880 Ultra */
	ID(PCI_DEVICE_ID_VIA_PT880ULTRA, "PT880 Ultra"),
	/* PT890 */
	ID(PCI_DEVICE_ID_VIA_8783_0, "PT890"),
	/* PM800/PN800/PM880/PN880 */
	ID(PCI_DEVICE_ID_VIA_PX8X0_0, "PM800/PN800/PM880/PN880"),
	/* KT880 */
	ID(PCI_DEVICE_ID_VIA_3269_0, "KT880"),
	/* KTxxx/Px8xx */
	ID(PCI_DEVICE_ID_VIA_83_87XX_1, "VT83xx/VT87xx/KTxxx/Px8xx"),
	/* P4M800 */
	ID(PCI_DEVICE_ID_VIA_3296_0, "P4M800"),
	/* P4M800CE */
	ID(PCI_DEVICE_ID_VIA_P4M800CE, "VT3314"),
	/* VT3324 / CX700 */
	ID(PCI_DEVICE_ID_VIA_VT3324, "CX700"),
	/* VT3336 - this is a chipset for AMD Athlon/K8 CPU. Due to K8's unique
	 * architecture, the AGP resource and behavior are different from
	 * the traditional AGP which resides only in chipset. AGP is used
	 * by 3D driver which wasn't available for the VT3336 and VT3364
	 * generation until now.  Unfortunately, by testing, VT3364 works
	 * but VT3336 doesn't. - explanation from via, just leave this as
	 * a placeholder to avoid future patches adding it back in.
	 */
#if 0
	ID(PCI_DEVICE_ID_VIA_VT3336, "VT3336"),
#endif
	/* P4M890 */
	ID(PCI_DEVICE_ID_VIA_P4M890, "P4M890"),
	/* P4M900 */
	ID(PCI_DEVICE_ID_VIA_VT3364, "P4M900"),
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_via_pci_table);

static DEFINE_SIMPLE_DEV_PM_OPS(agp_via_pm_ops, NULL, agp_via_resume);

static struct pci_driver agp_via_pci_driver = {
	.name		= "agpgart-via",
	.id_table	= agp_via_pci_table,
	.probe		= agp_via_probe,
	.remove		= agp_via_remove,
	.driver.pm      = &agp_via_pm_ops,
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

MODULE_DESCRIPTION("VIA AGPGART routines");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dave Jones");
