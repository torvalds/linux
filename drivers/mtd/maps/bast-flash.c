/* linux/drivers/mtd/maps/bast_flash.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Simtec Bast (EB2410ITX) NOR MTD Mapping driver
 *
 * Changelog:
 *	20-Sep-2004  BJD  Initial version
 *	17-Jan-2005  BJD  Add whole device if no partitions found
 *
 * $Id: bast-flash.c,v 1.2 2005/01/18 11:13:47 bjd Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/mach/flash.h>

#include <asm/arch/map.h>
#include <asm/arch/bast-map.h>
#include <asm/arch/bast-cpld.h>

#ifdef CONFIG_MTD_BAST_MAXSIZE
#define AREA_MAXSIZE (CONFIG_MTD_BAST_MAXSIZE * SZ_1M)
#else
#define AREA_MAXSIZE (32 * SZ_1M)
#endif

#define PFX "bast-flash: "

struct bast_flash_info {
	struct mtd_info		*mtd;
	struct map_info		 map;
	struct mtd_partition	*partitions;
	struct resource		*area;
};

static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };

static struct bast_flash_info *to_bast_info(struct device *dev)
{
	return (struct bast_flash_info *)dev_get_drvdata(dev);
}

static void bast_flash_setrw(int to)
{
	unsigned int val;
	unsigned long flags;

	local_irq_save(flags);
	val = __raw_readb(BAST_VA_CTRL3);
	
	if (to)
		val |= BAST_CPLD_CTRL3_ROMWEN;
	else
		val &= ~BAST_CPLD_CTRL3_ROMWEN;

	pr_debug("new cpld ctrl3=%02x\n", val);

	__raw_writeb(val, BAST_VA_CTRL3);
	local_irq_restore(flags);
}

static int bast_flash_remove(struct device *dev)
{
	struct bast_flash_info *info = to_bast_info(dev);

	dev_set_drvdata(dev, NULL);

	if (info == NULL) 
		return 0;

	if (info->map.virt != NULL)
		iounmap(info->map.virt);

	if (info->mtd) {
		del_mtd_partitions(info->mtd);
		map_destroy(info->mtd);
	}

	if (info->partitions)
		kfree(info->partitions);

	if (info->area) {
		release_resource(info->area);
		kfree(info->area);
	}
	
	kfree(info);

	return 0;
}

static int bast_flash_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bast_flash_info *info;
	struct resource *res;
	int err = 0;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL) {
		printk(KERN_ERR PFX "no memory for flash info\n");
		err = -ENOMEM;
		goto exit_error;
	}

	memzero(info, sizeof(*info));
	dev_set_drvdata(dev, info);

	res = pdev->resource;  /* assume that the flash has one resource */

	info->map.phys = res->start;
	info->map.size = res->end - res->start + 1;
	info->map.name = dev->bus_id;	
	info->map.bankwidth = 2;
	
	if (info->map.size > AREA_MAXSIZE)
		info->map.size = AREA_MAXSIZE;

	pr_debug("%s: area %08lx, size %ld\n", __FUNCTION__,
		 info->map.phys, info->map.size);
	
	info->area = request_mem_region(res->start, info->map.size,
					pdev->name);
	if (info->area == NULL) {
		printk(KERN_ERR PFX "cannot reserve flash memory region\n");
		err = -ENOENT;
		goto exit_error;
	}

	info->map.virt = ioremap(res->start, info->map.size);
	pr_debug("%s: virt at %08x\n", __FUNCTION__, (int)info->map.virt);

	if (info->map.virt == 0) {
		printk(KERN_ERR PFX "failed to ioremap() region\n");
		err = -EIO;
		goto exit_error;
	}
 
	simple_map_init(&info->map);

	/* enable the write to the flash area */

	bast_flash_setrw(1);

	/* probe for the device(s) */

	info->mtd = do_map_probe("jedec_probe", &info->map);
	if (info->mtd == NULL)
		info->mtd = do_map_probe("cfi_probe", &info->map);

	if (info->mtd == NULL) {
		printk(KERN_ERR PFX "map_probe() failed\n");
		err = -ENXIO;
		goto exit_error;
	}

	/* mark ourselves as the owner */
	info->mtd->owner = THIS_MODULE;

	err = parse_mtd_partitions(info->mtd, probes, &info->partitions, 0);
	if (err > 0) {
		err = add_mtd_partitions(info->mtd, info->partitions, err);
		if (err) 
			printk(KERN_ERR PFX "cannot add/parse partitions\n");
	} else {
		err = add_mtd_device(info->mtd);
	}

	if (err == 0)
		return 0;

	/* fall through to exit error */

 exit_error:
	bast_flash_remove(dev);
	return err;
}

static struct device_driver bast_flash_driver = {
	.name		= "bast-nor",
	.bus		= &platform_bus_type,
	.probe		= bast_flash_probe,
	.remove		= bast_flash_remove,
};

static int __init bast_flash_init(void)
{
	printk("BAST NOR-Flash Driver, (c) 2004 Simtec Electronics\n");
	return driver_register(&bast_flash_driver);
}

static void __exit bast_flash_exit(void)
{
	driver_unregister(&bast_flash_driver);
}

module_init(bast_flash_init);
module_exit(bast_flash_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("BAST MTD Map driver");
