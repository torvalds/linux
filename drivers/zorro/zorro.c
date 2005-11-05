/*
 *    $Id: zorro.c,v 1.1.2.1 1998/06/07 23:21:02 geert Exp $
 *
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

#include <asm/setup.h>
#include <asm/amigahw.h>

#include "zorro.h"


    /*
     *  Zorro Expansion Devices
     */

u_int zorro_num_autocon = 0;
struct zorro_dev zorro_autocon[ZORRO_NUM_AUTO];


    /*
     *  Single Zorro bus
     */

struct zorro_bus zorro_bus = {\
    .resources = {
	/* Zorro II regions (on Zorro II/III) */
	{ .name = "Zorro II exp", .start = 0x00e80000, .end = 0x00efffff },
	{ .name = "Zorro II mem", .start = 0x00200000, .end = 0x009fffff },
	/* Zorro III regions (on Zorro III only) */
	{ .name = "Zorro III exp", .start = 0xff000000, .end = 0xffffffff },
	{ .name = "Zorro III cfg", .start = 0x40000000, .end = 0x7fffffff }
    },
    .name = "Zorro bus"
};


    /*
     *  Find Zorro Devices
     */

struct zorro_dev *zorro_find_device(zorro_id id, struct zorro_dev *from)
{
    struct zorro_dev *z;

    if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO))
	return NULL;

    for (z = from ? from+1 : &zorro_autocon[0];
	 z < zorro_autocon+zorro_num_autocon;
	 z++)
	if (id == ZORRO_WILDCARD || id == z->id)
	    return z;
    return NULL;
}


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


static struct resource __init *zorro_find_parent_resource(struct zorro_dev *z)
{
    int i;

    for (i = 0; i < zorro_bus.num_resources; i++)
	if (zorro_resource_start(z) >= zorro_bus.resources[i].start &&
	    zorro_resource_end(z) <= zorro_bus.resources[i].end)
		return &zorro_bus.resources[i];
    return &iomem_resource;
}


    /*
     *  Initialization
     */

static int __init zorro_init(void)
{
    struct zorro_dev *z;
    unsigned int i;

    if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO))
	return 0;

    pr_info("Zorro: Probing AutoConfig expansion devices: %d device%s\n",
	   zorro_num_autocon, zorro_num_autocon == 1 ? "" : "s");

    /* Initialize the Zorro bus */
    INIT_LIST_HEAD(&zorro_bus.devices);
    strcpy(zorro_bus.dev.bus_id, "zorro");
    device_register(&zorro_bus.dev);

    /* Request the resources */
    zorro_bus.num_resources = AMIGAHW_PRESENT(ZORRO3) ? 4 : 2;
    for (i = 0; i < zorro_bus.num_resources; i++)
	request_resource(&iomem_resource, &zorro_bus.resources[i]);

    /* Register all devices */
    for (i = 0; i < zorro_num_autocon; i++) {
	z = &zorro_autocon[i];
	z->id = (z->rom.er_Manufacturer<<16) | (z->rom.er_Product<<8);
	if (z->id == ZORRO_PROD_GVP_EPC_BASE) {
	    /* GVP quirk */
	    unsigned long magic = zorro_resource_start(z)+0x8000;
	    z->id |= *(u16 *)ZTWO_VADDR(magic) & GVP_PRODMASK;
	}
	sprintf(z->name, "Zorro device %08x", z->id);
	zorro_name_device(z);
	z->resource.name = z->name;
	if (request_resource(zorro_find_parent_resource(z), &z->resource))
	    printk(KERN_ERR "Zorro: Address space collision on device %s "
		   "[%lx:%lx]\n",
		   z->name, zorro_resource_start(z), zorro_resource_end(z));
	sprintf(z->dev.bus_id, "%02x", i);
	z->dev.parent = &zorro_bus.dev;
	z->dev.bus = &zorro_bus_type;
	device_register(&z->dev);
	zorro_create_sysfs_dev_files(z);
    }

    /* Mark all available Zorro II memory */
    zorro_for_each_dev(z) {
	if (z->rom.er_Type & ERTF_MEMLIST)
	    mark_region(zorro_resource_start(z), zorro_resource_end(z)+1, 1);
    }

    /* Unmark all used Zorro II memory */
    for (i = 0; i < m68k_num_memory; i++)
	if (m68k_memory[i].addr < 16*1024*1024)
	    mark_region(m68k_memory[i].addr,
			m68k_memory[i].addr+m68k_memory[i].size, 0);

    return 0;
}

subsys_initcall(zorro_init);

EXPORT_SYMBOL(zorro_find_device);
EXPORT_SYMBOL(zorro_unused_z2ram);

MODULE_LICENSE("GPL");
