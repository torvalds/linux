/*
 * ck804xrom.c
 *
 * Normal mappings of chips in physical memory
 *
 * Dave Olsen <dolsen@lnxi.com>
 * Ryan Jackson <rjackson@lnxi.com>
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

#define ROM_PROBE_STEP_SIZE (64*1024)

#define DEV_CK804 1
#define DEV_MCP55 2

struct ck804xrom_window {
	void __iomem *virt;
	unsigned long phys;
	unsigned long size;
	struct list_head maps;
	struct resource rsrc;
	struct pci_dev *pdev;
};

struct ck804xrom_map_info {
	struct list_head list;
	struct map_info map;
	struct mtd_info *mtd;
	struct resource rsrc;
	char map_name[sizeof(MOD_NAME) + 2 + ADDRESS_NAME_LEN];
};

/*
 * The following applies to ck804 only:
 * The 2 bits controlling the window size are often set to allow reading
 * the BIOS, but too small to allow writing, since the lock registers are
 * 4MiB lower in the address space than the data.
 *
 * This is intended to prevent flashing the bios, perhaps accidentally.
 *
 * This parameter allows the normal driver to override the BIOS settings.
 *
 * The bits are 6 and 7.  If both bits are set, it is a 5MiB window.
 * If only the 7 Bit is set, it is a 4MiB window.  Otherwise, a
 * 64KiB window.
 *
 * The following applies to mcp55 only:
 * The 15 bits controlling the window size are distributed as follows: 
 * byte @0x88: bit 0..7
 * byte @0x8c: bit 8..15
 * word @0x90: bit 16..30
 * If all bits are enabled, we have a 16? MiB window
 * Please set win_size_bits to 0x7fffffff if you actually want to do something
 */
static uint win_size_bits = 0;
module_param(win_size_bits, uint, 0);
MODULE_PARM_DESC(win_size_bits, "ROM window size bits override, normally set by BIOS.");

static struct ck804xrom_window ck804xrom_window = {
	.maps = LIST_HEAD_INIT(ck804xrom_window.maps),
};

static void ck804xrom_cleanup(struct ck804xrom_window *window)
{
	struct ck804xrom_map_info *map, *scratch;
	u8 byte;

	if (window->pdev) {
		/* Disable writes through the rom window */
		pci_read_config_byte(window->pdev, 0x6d, &byte);
		pci_write_config_byte(window->pdev, 0x6d, byte & ~1);
	}

	/* Free all of the mtd devices */
	list_for_each_entry_safe(map, scratch, &window->maps, list) {
		if (map->rsrc.parent)
			release_resource(&map->rsrc);

		mtd_device_unregister(map->mtd);
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


static int __init ck804xrom_init_one(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	static char *rom_probe_types[] = { "cfi_probe", "jedec_probe", NULL };
	u8 byte;
	u16 word;
	struct ck804xrom_window *window = &ck804xrom_window;
	struct ck804xrom_map_info *map = NULL;
	unsigned long map_top;

	/* Remember the pci dev I find the window in */
	window->pdev = pci_dev_get(pdev);

	switch (ent->driver_data) {
	case DEV_CK804:
		/* Enable the selected rom window.  This is often incorrectly
		 * set up by the BIOS, and the 4MiB offset for the lock registers
		 * requires the full 5MiB of window space.
		 *
		 * This 'write, then read' approach leaves the bits for
		 * other uses of the hardware info.
		 */
		pci_read_config_byte(pdev, 0x88, &byte);
		pci_write_config_byte(pdev, 0x88, byte | win_size_bits );

		/* Assume the rom window is properly setup, and find it's size */
		pci_read_config_byte(pdev, 0x88, &byte);

		if ((byte & ((1<<7)|(1<<6))) == ((1<<7)|(1<<6)))
			window->phys = 0xffb00000; /* 5MiB */
		else if ((byte & (1<<7)) == (1<<7))
			window->phys = 0xffc00000; /* 4MiB */
		else
			window->phys = 0xffff0000; /* 64KiB */
		break;

	case DEV_MCP55:
		pci_read_config_byte(pdev, 0x88, &byte);
		pci_write_config_byte(pdev, 0x88, byte | (win_size_bits & 0xff));

		pci_read_config_byte(pdev, 0x8c, &byte);
		pci_write_config_byte(pdev, 0x8c, byte | ((win_size_bits & 0xff00) >> 8));

		pci_read_config_word(pdev, 0x90, &word);
		pci_write_config_word(pdev, 0x90, word | ((win_size_bits & 0x7fff0000) >> 16));

		window->phys = 0xff000000; /* 16MiB, hardcoded for now */
		break;
	}

	window->size = 0xffffffffUL - window->phys + 1UL;

	/*
	 * Try to reserve the window mem region.  If this fails then
	 * it is likely due to a fragment of the window being
	 * "reserved" by the BIOS.  In the case that the
	 * request_mem_region() fails then once the rom size is
	 * discovered we will try to reserve the unreserved fragment.
	 */
	window->rsrc.name = MOD_NAME;
	window->rsrc.start = window->phys;
	window->rsrc.end   = window->phys + window->size - 1;
	window->rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, &window->rsrc)) {
		window->rsrc.parent = NULL;
		printk(KERN_ERR MOD_NAME
		       " %s(): Unable to register resource %pR - kernel bug?\n",
			__func__, &window->rsrc);
	}


	/* Enable writes through the rom window */
	pci_read_config_byte(pdev, 0x6d, &byte);
	pci_write_config_byte(pdev, 0x6d, byte | 1);

	/* FIXME handle registers 0x80 - 0x8C the bios region locks */

	/* For write accesses caches are useless */
	window->virt = ioremap_nocache(window->phys, window->size);
	if (!window->virt) {
		printk(KERN_ERR MOD_NAME ": ioremap(%08lx, %08lx) failed\n",
			window->phys, window->size);
		goto out;
	}

	/* Get the first address to look for a rom chip at */
	map_top = window->phys;
#if 1
	/* The probe sequence run over the firmware hub lock
	 * registers sets them to 0x7 (no access).
	 * Probe at most the last 4MiB of the address space.
	 */
	if (map_top < 0xffc00000)
		map_top = 0xffc00000;
#endif
	/* Loop  through and look for rom chips.  Since we don't know the
	 * starting address for each chip, probe every ROM_PROBE_STEP_SIZE
	 * bytes from the starting address of the window.
	 */
	while((map_top - 1) < 0xffffffffUL) {
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

		/* There is no generic VPP support */
		for(map->map.bankwidth = 32; map->map.bankwidth;
			map->map.bankwidth >>= 1)
		{
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
		if (mtd_device_register(map->mtd, NULL, 0)) {
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
		ck804xrom_cleanup(window);
		return -ENODEV;
	}
	return 0;
}


static void ck804xrom_remove_one(struct pci_dev *pdev)
{
	struct ck804xrom_window *window = &ck804xrom_window;

	ck804xrom_cleanup(window);
}

static struct pci_device_id ck804xrom_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0051), .driver_data = DEV_CK804 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0360), .driver_data = DEV_MCP55 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0361), .driver_data = DEV_MCP55 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0362), .driver_data = DEV_MCP55 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0363), .driver_data = DEV_MCP55 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0364), .driver_data = DEV_MCP55 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0365), .driver_data = DEV_MCP55 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0366), .driver_data = DEV_MCP55 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x0367), .driver_data = DEV_MCP55 },
	{ 0, }
};

#if 0
MODULE_DEVICE_TABLE(pci, ck804xrom_pci_tbl);

static struct pci_driver ck804xrom_driver = {
	.name =		MOD_NAME,
	.id_table =	ck804xrom_pci_tbl,
	.probe =	ck804xrom_init_one,
	.remove =	ck804xrom_remove_one,
};
#endif

static int __init init_ck804xrom(void)
{
	struct pci_dev *pdev;
	struct pci_device_id *id;
	int retVal;
	pdev = NULL;

	for(id = ck804xrom_pci_tbl; id->vendor; id++) {
		pdev = pci_get_device(id->vendor, id->device, NULL);
		if (pdev)
			break;
	}
	if (pdev) {
		retVal = ck804xrom_init_one(pdev, id);
		pci_dev_put(pdev);
		return retVal;
	}
	return -ENXIO;
#if 0
	return pci_register_driver(&ck804xrom_driver);
#endif
}

static void __exit cleanup_ck804xrom(void)
{
	ck804xrom_remove_one(ck804xrom_window.pdev);
}

module_init(init_ck804xrom);
module_exit(cleanup_ck804xrom);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Biederman <ebiederman@lnxi.com>, Dave Olsen <dolsen@lnxi.com>");
MODULE_DESCRIPTION("MTD map driver for BIOS chips on the Nvidia ck804 southbridge");

