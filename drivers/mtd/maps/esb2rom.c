/*
 * esb2rom.c
 *
 * Normal mappings of flash chips in physical memory
 * through the Intel ESB2 Southbridge.
 *
 * This was derived from ichxrom.c in May 2006 by
 *	Lew Glendenning <lglendenning@lnxi.com>
 *
 * Eric Biederman, of course, was a major help in this effort.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/flashchip.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/list.h>

#define MOD_NAME KBUILD_BASENAME

#define ADDRESS_NAME_LEN 18

#define ROM_PROBE_STEP_SIZE (64*1024) /* 64KiB */

#define BIOS_CNTL		0xDC
#define BIOS_LOCK_ENABLE	0x02
#define BIOS_WRITE_ENABLE	0x01

/* This became a 16-bit register, and EN2 has disappeared */
#define FWH_DEC_EN1	0xD8
#define FWH_F8_EN	0x8000
#define FWH_F0_EN	0x4000
#define FWH_E8_EN	0x2000
#define FWH_E0_EN	0x1000
#define FWH_D8_EN	0x0800
#define FWH_D0_EN	0x0400
#define FWH_C8_EN	0x0200
#define FWH_C0_EN	0x0100
#define FWH_LEGACY_F_EN	0x0080
#define FWH_LEGACY_E_EN	0x0040
/* reserved  0x0020 and 0x0010 */
#define FWH_70_EN	0x0008
#define FWH_60_EN	0x0004
#define FWH_50_EN	0x0002
#define FWH_40_EN	0x0001

/* these are 32-bit values */
#define FWH_SEL1	0xD0
#define FWH_SEL2	0xD4

#define FWH_8MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN | FWH_D0_EN | FWH_C8_EN | FWH_C0_EN | \
			 FWH_70_EN | FWH_60_EN | FWH_50_EN | FWH_40_EN)

#define FWH_7MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN | FWH_D0_EN | FWH_C8_EN | FWH_C0_EN | \
			 FWH_70_EN | FWH_60_EN | FWH_50_EN)

#define FWH_6MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN | FWH_D0_EN | FWH_C8_EN | FWH_C0_EN | \
			 FWH_70_EN | FWH_60_EN)

#define FWH_5MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN | FWH_D0_EN | FWH_C8_EN | FWH_C0_EN | \
			 FWH_70_EN)

#define FWH_4MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN | FWH_D0_EN | FWH_C8_EN | FWH_C0_EN)

#define FWH_3_5MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN | FWH_D0_EN | FWH_C8_EN)

#define FWH_3MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN | FWH_D0_EN)

#define FWH_2_5MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN | \
			 FWH_D8_EN)

#define FWH_2MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN | FWH_E0_EN)

#define FWH_1_5MiB	(FWH_F8_EN | FWH_F0_EN | FWH_E8_EN)

#define FWH_1MiB	(FWH_F8_EN | FWH_F0_EN)

#define FWH_0_5MiB	(FWH_F8_EN)


struct esb2rom_window {
	void __iomem* virt;
	unsigned long phys;
	unsigned long size;
	struct list_head maps;
	struct resource rsrc;
	struct pci_dev *pdev;
};

struct esb2rom_map_info {
	struct list_head list;
	struct map_info map;
	struct mtd_info *mtd;
	struct resource rsrc;
	char map_name[sizeof(MOD_NAME) + 2 + ADDRESS_NAME_LEN];
};

static struct esb2rom_window esb2rom_window = {
	.maps = LIST_HEAD_INIT(esb2rom_window.maps),
};

static void esb2rom_cleanup(struct esb2rom_window *window)
{
	struct esb2rom_map_info *map, *scratch;
	u8 byte;

	/* Disable writes through the rom window */
	pci_read_config_byte(window->pdev, BIOS_CNTL, &byte);
	pci_write_config_byte(window->pdev, BIOS_CNTL,
		byte & ~BIOS_WRITE_ENABLE);

	/* Free all of the mtd devices */
	list_for_each_entry_safe(map, scratch, &window->maps, list) {
		if (map->rsrc.parent)
			release_resource(&map->rsrc);
		del_mtd_device(map->mtd);
		map_destroy(map->mtd);
		list_del(&map->list);
		kfree(map);
	}
	if (window->rsrc.parent)
		release_resource(&window->rsrc);
	if (window->virt) {
		iounmap(window->virt);
		window->virt = NULL;
		window->phys = 0;
		window->size = 0;
	}
	pci_dev_put(window->pdev);
}

static int __devinit esb2rom_init_one(struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	static char *rom_probe_types[] = { "cfi_probe", "jedec_probe", NULL };
	struct esb2rom_window *window = &esb2rom_window;
	struct esb2rom_map_info *map = NULL;
	unsigned long map_top;
	u8 byte;
	u16 word;

	/* For now I just handle the ecb2 and I assume there
	 * are not a lot of resources up at the top of the address
	 * space.  It is possible to handle other devices in the
	 * top 16MiB but it is very painful.  Also since
	 * you can only really attach a FWH to an ICHX there
	 * a number of simplifications you can make.
	 *
	 * Also you can page firmware hubs if an 8MiB window isn't enough
	 * but don't currently handle that case either.
	 */
	window->pdev = pci_dev_get(pdev);

	/* RLG:  experiment 2.  Force the window registers to the widest values */

/*
	pci_read_config_word(pdev, FWH_DEC_EN1, &word);
	printk(KERN_DEBUG "Original FWH_DEC_EN1 : %x\n", word);
	pci_write_config_byte(pdev, FWH_DEC_EN1, 0xff);
	pci_read_config_byte(pdev, FWH_DEC_EN1, &byte);
	printk(KERN_DEBUG "New FWH_DEC_EN1 : %x\n", byte);

	pci_read_config_byte(pdev, FWH_DEC_EN2, &byte);
	printk(KERN_DEBUG "Original FWH_DEC_EN2 : %x\n", byte);
	pci_write_config_byte(pdev, FWH_DEC_EN2, 0x0f);
	pci_read_config_byte(pdev, FWH_DEC_EN2, &byte);
	printk(KERN_DEBUG "New FWH_DEC_EN2 : %x\n", byte);
*/

	/* Find a region continuous to the end of the ROM window  */
	window->phys = 0;
	pci_read_config_word(pdev, FWH_DEC_EN1, &word);
	printk(KERN_DEBUG "pci_read_config_word : %x\n", word);

	if ((word & FWH_8MiB) == FWH_8MiB)
		window->phys = 0xff400000;
	else if ((word & FWH_7MiB) == FWH_7MiB)
		window->phys = 0xff500000;
	else if ((word & FWH_6MiB) == FWH_6MiB)
		window->phys = 0xff600000;
	else if ((word & FWH_5MiB) == FWH_5MiB)
		window->phys = 0xFF700000;
	else if ((word & FWH_4MiB) == FWH_4MiB)
		window->phys = 0xffc00000;
	else if ((word & FWH_3_5MiB) == FWH_3_5MiB)
		window->phys = 0xffc80000;
	else if ((word & FWH_3MiB) == FWH_3MiB)
		window->phys = 0xffd00000;
	else if ((word & FWH_2_5MiB) == FWH_2_5MiB)
		window->phys = 0xffd80000;
	else if ((word & FWH_2MiB) == FWH_2MiB)
		window->phys = 0xffe00000;
	else if ((word & FWH_1_5MiB) == FWH_1_5MiB)
		window->phys = 0xffe80000;
	else if ((word & FWH_1MiB) == FWH_1MiB)
		window->phys = 0xfff00000;
	else if ((word & FWH_0_5MiB) == FWH_0_5MiB)
		window->phys = 0xfff80000;

	if (window->phys == 0) {
		printk(KERN_ERR MOD_NAME ": Rom window is closed\n");
		goto out;
	}

	/* reserved  0x0020 and 0x0010 */
	window->phys -= 0x400000UL;
	window->size = (0xffffffffUL - window->phys) + 1UL;

	/* Enable writes through the rom window */
	pci_read_config_byte(pdev, BIOS_CNTL, &byte);
	if (!(byte & BIOS_WRITE_ENABLE)  && (byte & (BIOS_LOCK_ENABLE))) {
		/* The BIOS will generate an error if I enable
		 * this device, so don't even try.
		 */
		printk(KERN_ERR MOD_NAME ": firmware access control, I can't enable writes\n");
		goto out;
	}
	pci_write_config_byte(pdev, BIOS_CNTL, byte | BIOS_WRITE_ENABLE);

	/*
	 * Try to reserve the window mem region.  If this fails then
	 * it is likely due to the window being "reseved" by the BIOS.
	 */
	window->rsrc.name = MOD_NAME;
	window->rsrc.start = window->phys;
	window->rsrc.end   = window->phys + window->size - 1;
	window->rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, &window->rsrc)) {
		window->rsrc.parent = NULL;
		printk(KERN_DEBUG MOD_NAME ": "
		       "%s(): Unable to register resource %pR - kernel bug?\n",
			__func__, &window->rsrc);
	}

	/* Map the firmware hub into my address space. */
	window->virt = ioremap_nocache(window->phys, window->size);
	if (!window->virt) {
		printk(KERN_ERR MOD_NAME ": ioremap(%08lx, %08lx) failed\n",
			window->phys, window->size);
		goto out;
	}

	/* Get the first address to look for an rom chip at */
	map_top = window->phys;
	if ((window->phys & 0x3fffff) != 0) {
		/* if not aligned on 4MiB, look 4MiB lower in address space */
		map_top = window->phys + 0x400000;
	}
#if 1
	/* The probe sequence run over the firmware hub lock
	 * registers sets them to 0x7 (no access).
	 * (Insane hardware design, but most copied Intel's.)
	 * ==> Probe at most the last 4M of the address space.
	 */
	if (map_top < 0xffc00000)
		map_top = 0xffc00000;
#endif
	/* Loop through and look for rom chips */
	while ((map_top - 1) < 0xffffffffUL) {
		struct cfi_private *cfi;
		unsigned long offset;
		int i;

		if (!map)
			map = kmalloc(sizeof(*map), GFP_KERNEL);
		if (!map) {
			printk(KERN_ERR MOD_NAME ": kmalloc failed");
			goto out;
		}
		memset(map, 0, sizeof(*map));
		INIT_LIST_HEAD(&map->list);
		map->map.name = map->map_name;
		map->map.phys = map_top;
		offset = map_top - window->phys;
		map->map.virt = (void __iomem *)
			(((unsigned long)(window->virt)) + offset);
		map->map.size = 0xffffffffUL - map_top + 1UL;
		/* Set the name of the map to the address I am trying */
		sprintf(map->map_name, "%s @%08Lx",
			MOD_NAME, (unsigned long long)map->map.phys);

		/* Firmware hubs only use vpp when being programmed
		 * in a factory setting.  So in-place programming
		 * needs to use a different method.
		 */
		for(map->map.bankwidth = 32; map->map.bankwidth;
			map->map.bankwidth >>= 1) {
			char **probe_type;
			/* Skip bankwidths that are not supported */
			if (!map_bankwidth_supported(map->map.bankwidth))
				continue;

			/* Setup the map methods */
			simple_map_init(&map->map);

			/* Try all of the probe methods */
			probe_type = rom_probe_types;
			for(; *probe_type; probe_type++) {
				map->mtd = do_map_probe(*probe_type, &map->map);
				if (map->mtd)
					goto found;
			}
		}
		map_top += ROM_PROBE_STEP_SIZE;
		continue;
	found:
		/* Trim the size if we are larger than the map */
		if (map->mtd->size > map->map.size) {
			printk(KERN_WARNING MOD_NAME
				" rom(%llu) larger than window(%lu). fixing...\n",
				(unsigned long long)map->mtd->size, map->map.size);
			map->mtd->size = map->map.size;
		}
		if (window->rsrc.parent) {
			/*
			 * Registering the MTD device in iomem may not be possible
			 * if there is a BIOS "reserved" and BUSY range.  If this
			 * fails then continue anyway.
			 */
			map->rsrc.name  = map->map_name;
			map->rsrc.start = map->map.phys;
			map->rsrc.end   = map->map.phys + map->mtd->size - 1;
			map->rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
			if (request_resource(&window->rsrc, &map->rsrc)) {
				printk(KERN_ERR MOD_NAME
					": cannot reserve MTD resource\n");
				map->rsrc.parent = NULL;
			}
		}

		/* Make the whole region visible in the map */
		map->map.virt = window->virt;
		map->map.phys = window->phys;
		cfi = map->map.fldrv_priv;
		for(i = 0; i < cfi->numchips; i++)
			cfi->chips[i].start += offset;

		/* Now that the mtd devices is complete claim and export it */
		map->mtd->owner = THIS_MODULE;
		if (add_mtd_device(map->mtd)) {
			map_destroy(map->mtd);
			map->mtd = NULL;
			goto out;
		}

		/* Calculate the new value of map_top */
		map_top += map->mtd->size;

		/* File away the map structure */
		list_add(&map->list, &window->maps);
		map = NULL;
	}

 out:
	/* Free any left over map structures */
	kfree(map);

	/* See if I have any map structures */
	if (list_empty(&window->maps)) {
		esb2rom_cleanup(window);
		return -ENODEV;
	}
	return 0;
}

static void __devexit esb2rom_remove_one (struct pci_dev *pdev)
{
	struct esb2rom_window *window = &esb2rom_window;
	esb2rom_cleanup(window);
}

static struct pci_device_id esb2rom_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_0,
	  PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_0,
	  PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_0,
	  PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_0,
	  PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_1,
	  PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB2_0,
	  PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, },
};

#if 0
MODULE_DEVICE_TABLE(pci, esb2rom_pci_tbl);

static struct pci_driver esb2rom_driver = {
	.name =		MOD_NAME,
	.id_table =	esb2rom_pci_tbl,
	.probe =	esb2rom_init_one,
	.remove =	esb2rom_remove_one,
};
#endif

static int __init init_esb2rom(void)
{
	struct pci_dev *pdev;
	struct pci_device_id *id;
	int retVal;

	pdev = NULL;
	for (id = esb2rom_pci_tbl; id->vendor; id++) {
		printk(KERN_DEBUG "device id = %x\n", id->device);
		pdev = pci_get_device(id->vendor, id->device, NULL);
		if (pdev) {
			printk(KERN_DEBUG "matched device = %x\n", id->device);
			break;
		}
	}
	if (pdev) {
		printk(KERN_DEBUG "matched device id %x\n", id->device);
		retVal = esb2rom_init_one(pdev, &esb2rom_pci_tbl[0]);
		pci_dev_put(pdev);
		printk(KERN_DEBUG "retVal = %d\n", retVal);
		return retVal;
	}
	return -ENXIO;
#if 0
	return pci_register_driver(&esb2rom_driver);
#endif
}

static void __exit cleanup_esb2rom(void)
{
	esb2rom_remove_one(esb2rom_window.pdev);
}

module_init(init_esb2rom);
module_exit(cleanup_esb2rom);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lew Glendenning <lglendenning@lnxi.com>");
MODULE_DESCRIPTION("MTD map driver for BIOS chips on the ESB2 southbridge");
