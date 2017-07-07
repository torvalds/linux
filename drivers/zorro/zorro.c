/*
 *    Zorro Bus Services
 *
 *    Copyright (C) 1995-2003 Geert Uytterhoeven
 *
 *    This file is subject to the terms and conditions of the GNU General Public
 *    License.  See the file COPYING in the main directory of this archive
 *    for more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/zorro.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/setup.h>
#include <asm/amigahw.h>

#include "zorro.h"


    /*
     *  Zorro Expansion Devices
     */

unsigned int zorro_num_autocon;
struct zorro_dev_init zorro_autocon_init[ZORRO_NUM_AUTO] __initdata;
struct zorro_dev *zorro_autocon;


    /*
     *  Zorro bus
     */

struct zorro_bus {
	struct device dev;
	struct zorro_dev devices[0];
};


    /*
     *  Find Zorro Devices
     */

struct zorro_dev *zorro_find_device(zorro_id id, struct zorro_dev *from)
{
	struct zorro_dev *z;

	if (!zorro_num_autocon)
		return NULL;

	for (z = from ? from+1 : &zorro_autocon[0];
	     z < zorro_autocon+zorro_num_autocon;
	     z++)
		if (id == ZORRO_WILDCARD || id == z->id)
			return z;
	return NULL;
}
EXPORT_SYMBOL(zorro_find_device);


    /*
     *  Bitmask indicating portions of available Zorro II RAM that are unused
     *  by the system. Every bit represents a 64K chunk, for a maximum of 8MB
     *  (128 chunks, physical 0x00200000-0x009fffff).
     *
     *  If you want to use (= allocate) portions of this RAM, you should clear
     *  the corresponding bits.
     *
     *  Possible uses:
     *      - z2ram device
     *      - SCSI DMA bounce buffers
     *
     *  FIXME: use the normal resource management
     */

DECLARE_BITMAP(zorro_unused_z2ram, 128);
EXPORT_SYMBOL(zorro_unused_z2ram);


static void __init mark_region(unsigned long start, unsigned long end,
			       int flag)
{
	if (flag)
		start += Z2RAM_CHUNKMASK;
	else
		end += Z2RAM_CHUNKMASK;
	start &= ~Z2RAM_CHUNKMASK;
	end &= ~Z2RAM_CHUNKMASK;

	if (end <= Z2RAM_START || start >= Z2RAM_END)
		return;
	start = start < Z2RAM_START ? 0x00000000 : start-Z2RAM_START;
	end = end > Z2RAM_END ? Z2RAM_SIZE : end-Z2RAM_START;
	while (start < end) {
		u32 chunk = start>>Z2RAM_CHUNKSHIFT;
		if (flag)
			set_bit(chunk, zorro_unused_z2ram);
		else
			clear_bit(chunk, zorro_unused_z2ram);
		start += Z2RAM_CHUNKSIZE;
	}
}


static struct resource __init *zorro_find_parent_resource(
	struct platform_device *bridge, struct zorro_dev *z)
{
	int i;

	for (i = 0; i < bridge->num_resources; i++) {
		struct resource *r = &bridge->resource[i];
		if (zorro_resource_start(z) >= r->start &&
		    zorro_resource_end(z) <= r->end)
			return r;
	}
	return &iomem_resource;
}



static int __init amiga_zorro_probe(struct platform_device *pdev)
{
	struct zorro_bus *bus;
	struct zorro_dev_init *zi;
	struct zorro_dev *z;
	struct resource *r;
	unsigned int i;
	int error;

	/* Initialize the Zorro bus */
	bus = kzalloc(sizeof(*bus) +
		      zorro_num_autocon * sizeof(bus->devices[0]),
		      GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	zorro_autocon = bus->devices;
	bus->dev.parent = &pdev->dev;
	dev_set_name(&bus->dev, zorro_bus_type.name);
	error = device_register(&bus->dev);
	if (error) {
		pr_err("Zorro: Error registering zorro_bus\n");
		put_device(&bus->dev);
		kfree(bus);
		return error;
	}
	platform_set_drvdata(pdev, bus);

	pr_info("Zorro: Probing AutoConfig expansion devices: %u device%s\n",
		 zorro_num_autocon, zorro_num_autocon == 1 ? "" : "s");

	/* First identify all devices ... */
	for (i = 0; i < zorro_num_autocon; i++) {
		zi = &zorro_autocon_init[i];
		z = &zorro_autocon[i];

		z->rom = zi->rom;
		z->id = (be16_to_cpu(z->rom.er_Manufacturer) << 16) |
			(z->rom.er_Product << 8);
		if (z->id == ZORRO_PROD_GVP_EPC_BASE) {
			/* GVP quirk */
			unsigned long magic = zi->boardaddr + 0x8000;
			z->id |= *(u16 *)ZTWO_VADDR(magic) & GVP_PRODMASK;
		}
		z->slotaddr = zi->slotaddr;
		z->slotsize = zi->slotsize;
		sprintf(z->name, "Zorro device %08x", z->id);
		zorro_name_device(z);
		z->resource.start = zi->boardaddr;
		z->resource.end = zi->boardaddr + zi->boardsize - 1;
		z->resource.name = z->name;
		r = zorro_find_parent_resource(pdev, z);
		error = request_resource(r, &z->resource);
		if (error)
			dev_err(&bus->dev,
				"Address space collision on device %s %pR\n",
				z->name, &z->resource);
		z->dev.parent = &bus->dev;
		z->dev.bus = &zorro_bus_type;
		z->dev.id = i;
	}

	/* ... then register them */
	for (i = 0; i < zorro_num_autocon; i++) {
		z = &zorro_autocon[i];
		error = device_register(&z->dev);
		if (error) {
			dev_err(&bus->dev, "Error registering device %s\n",
				z->name);
			put_device(&z->dev);
			continue;
		}
	}

	/* Mark all available Zorro II memory */
	zorro_for_each_dev(z) {
		if (z->rom.er_Type & ERTF_MEMLIST)
			mark_region(zorro_resource_start(z),
				    zorro_resource_end(z)+1, 1);
	}

	/* Unmark all used Zorro II memory */
	for (i = 0; i < m68k_num_memory; i++)
		if (m68k_memory[i].addr < 16*1024*1024)
			mark_region(m68k_memory[i].addr,
				    m68k_memory[i].addr+m68k_memory[i].size,
				    0);

	return 0;
}

static struct platform_driver amiga_zorro_driver = {
	.driver   = {
		.name	= "amiga-zorro",
	},
};

static int __init amiga_zorro_init(void)
{
	return platform_driver_probe(&amiga_zorro_driver, amiga_zorro_probe);
}

module_init(amiga_zorro_init);

MODULE_LICENSE("GPL");
