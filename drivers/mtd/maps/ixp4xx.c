/*
 * $Id: ixp4xx.c,v 1.13 2005/11/16 16:23:21 dvrabel Exp $
 *
 * drivers/mtd/maps/ixp4xx.c
 *
 * MTD Map file for IXP4XX based systems. Please do not make per-board
 * changes in here. If your board needs special setup, do it in your
 * platform level code in arch/arm/mach-ixp4xx/board-setup.c
 *
 * Original Author: Intel Corporation
 * Maintainer: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright (C) 2002 Intel Corporation
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/mach/flash.h>

#include <linux/reboot.h>

/*
 * Read/write a 16 bit word from flash address 'addr'.
 *
 * When the cpu is in little-endian mode it swizzles the address lines
 * ('address coherency') so we need to undo the swizzling to ensure commands
 * and the like end up on the correct flash address.
 *
 * To further complicate matters, due to the way the expansion bus controller
 * handles 32 bit reads, the byte stream ABCD is stored on the flash as:
 *     D15    D0
 *     +---+---+
 *     | A | B | 0
 *     +---+---+
 *     | C | D | 2
 *     +---+---+
 * This means that on LE systems each 16 bit word must be swapped. Note that
 * this requires CONFIG_MTD_CFI_BE_BYTE_SWAP to be enabled to 'unswap' the CFI
 * data and other flash commands which are always in D7-D0.
 */
#ifndef __ARMEB__
#ifndef CONFIG_MTD_CFI_BE_BYTE_SWAP
#  error CONFIG_MTD_CFI_BE_BYTE_SWAP required
#endif

static inline u16 flash_read16(void __iomem *addr)
{
	return be16_to_cpu(__raw_readw((void __iomem *)((unsigned long)addr ^ 0x2)));
}

static inline void flash_write16(u16 d, void __iomem *addr)
{
	__raw_writew(cpu_to_be16(d), (void __iomem *)((unsigned long)addr ^ 0x2));
}

#define	BYTE0(h)	((h) & 0xFF)
#define	BYTE1(h)	(((h) >> 8) & 0xFF)

#else

static inline u16 flash_read16(const void __iomem *addr)
{
	return __raw_readw(addr);
}

static inline void flash_write16(u16 d, void __iomem *addr)
{
	__raw_writew(d, addr);
}

#define	BYTE0(h)	(((h) >> 8) & 0xFF)
#define	BYTE1(h)	((h) & 0xFF)
#endif

static map_word ixp4xx_read16(struct map_info *map, unsigned long ofs)
{
	map_word val;
	val.x[0] = flash_read16(map->virt + ofs);
	return val;
}

/*
 * The IXP4xx expansion bus only allows 16-bit wide acceses
 * when attached to a 16-bit wide device (such as the 28F128J3A),
 * so we can't just memcpy_fromio().
 */
static void ixp4xx_copy_from(struct map_info *map, void *to,
			     unsigned long from, ssize_t len)
{
	u8 *dest = (u8 *) to;
	void __iomem *src = map->virt + from;

	if (len <= 0)
		return;

	if (from & 1) {
		*dest++ = BYTE1(flash_read16(src));
                src++;
		--len;
	}

	while (len >= 2) {
		u16 data = flash_read16(src);
		*dest++ = BYTE0(data);
		*dest++ = BYTE1(data);
		src += 2;
		len -= 2;
        }

	if (len > 0)
		*dest++ = BYTE0(flash_read16(src));
}

/*
 * Unaligned writes are ignored, causing the 8-bit
 * probe to fail and proceed to the 16-bit probe (which succeeds).
 */
static void ixp4xx_probe_write16(struct map_info *map, map_word d, unsigned long adr)
{
	if (!(adr & 1))
		flash_write16(d.x[0], map->virt + adr);
}

/*
 * Fast write16 function without the probing check above
 */
static void ixp4xx_write16(struct map_info *map, map_word d, unsigned long adr)
{
	flash_write16(d.x[0], map->virt + adr);
}

struct ixp4xx_flash_info {
	struct mtd_info *mtd;
	struct map_info map;
	struct mtd_partition *partitions;
	struct resource *res;
};

static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };

static int ixp4xx_flash_remove(struct platform_device *dev)
{
	struct flash_platform_data *plat = dev->dev.platform_data;
	struct ixp4xx_flash_info *info = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if(!info)
		return 0;

	if (info->mtd) {
		del_mtd_partitions(info->mtd);
		map_destroy(info->mtd);
	}
	if (info->map.virt)
		iounmap(info->map.virt);

	kfree(info->partitions);

	if (info->res) {
		release_resource(info->res);
		kfree(info->res);
	}

	if (plat->exit)
		plat->exit();

	return 0;
}

static int ixp4xx_flash_probe(struct platform_device *dev)
{
	struct flash_platform_data *plat = dev->dev.platform_data;
	struct ixp4xx_flash_info *info;
	int err = -1;

	if (!plat)
		return -ENODEV;

	if (plat->init) {
		err = plat->init();
		if (err)
			return err;
	}

	info = kmalloc(sizeof(struct ixp4xx_flash_info), GFP_KERNEL);
	if(!info) {
		err = -ENOMEM;
		goto Error;
	}
	memzero(info, sizeof(struct ixp4xx_flash_info));

	platform_set_drvdata(dev, info);

	/*
	 * Tell the MTD layer we're not 1:1 mapped so that it does
	 * not attempt to do a direct access on us.
	 */
	info->map.phys = NO_XIP;
	info->map.size = dev->resource->end - dev->resource->start + 1;

	/*
	 * We only support 16-bit accesses for now. If and when
	 * any board use 8-bit access, we'll fixup the driver to
	 * handle that.
	 */
	info->map.bankwidth = 2;
	info->map.name = dev->dev.bus_id;
	info->map.read = ixp4xx_read16,
	info->map.write = ixp4xx_probe_write16,
	info->map.copy_from = ixp4xx_copy_from,

	info->res = request_mem_region(dev->resource->start,
			dev->resource->end - dev->resource->start + 1,
			"IXP4XXFlash");
	if (!info->res) {
		printk(KERN_ERR "IXP4XXFlash: Could not reserve memory region\n");
		err = -ENOMEM;
		goto Error;
	}

	info->map.virt = ioremap(dev->resource->start,
				 dev->resource->end - dev->resource->start + 1);
	if (!info->map.virt) {
		printk(KERN_ERR "IXP4XXFlash: Failed to ioremap region\n");
		err = -EIO;
		goto Error;
	}

	info->mtd = do_map_probe(plat->map_name, &info->map);
	if (!info->mtd) {
		printk(KERN_ERR "IXP4XXFlash: map_probe failed\n");
		err = -ENXIO;
		goto Error;
	}
	info->mtd->owner = THIS_MODULE;

	/* Use the fast version */
	info->map.write = ixp4xx_write16,

	err = parse_mtd_partitions(info->mtd, probes, &info->partitions, 0);
	if (err > 0) {
		err = add_mtd_partitions(info->mtd, info->partitions, err);
		if(err)
			printk(KERN_ERR "Could not parse partitions\n");
	}

	if (err)
		goto Error;

	return 0;

Error:
	ixp4xx_flash_remove(dev);
	return err;
}

static struct platform_driver ixp4xx_flash_driver = {
	.probe		= ixp4xx_flash_probe,
	.remove		= ixp4xx_flash_remove,
	.driver		= {
		.name	= "IXP4XX-Flash",
	},
};

static int __init ixp4xx_flash_init(void)
{
	return platform_driver_register(&ixp4xx_flash_driver);
}

static void __exit ixp4xx_flash_exit(void)
{
	platform_driver_unregister(&ixp4xx_flash_driver);
}


module_init(ixp4xx_flash_init);
module_exit(ixp4xx_flash_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD map driver for Intel IXP4xx systems");
MODULE_AUTHOR("Deepak Saxena");
