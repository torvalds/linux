/* drivers/mtd/maps/plat-ram.c
 *
 * (c) 2004-2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/SWLINUX/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Generic platfrom device based RAM map
 *
 * $Id: plat-ram.c,v 1.7 2005/11/07 11:14:28 gleixner Exp $
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
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/plat-ram.h>

#include <asm/io.h>

/* private structure for each mtd platform ram device created */

struct platram_info {
	struct device		*dev;
	struct mtd_info		*mtd;
	struct map_info		 map;
	struct mtd_partition	*partitions;
	struct resource		*area;
	struct platdata_mtd_ram	*pdata;
};

/* to_platram_info()
 *
 * device private data to struct platram_info conversion
*/

static inline struct platram_info *to_platram_info(struct device *dev)
{
	return (struct platram_info *)dev_get_drvdata(dev);
}

/* platram_setrw
 *
 * call the platform device's set rw/ro control
 *
 * to = 0 => read-only
 *    = 1 => read-write
*/

static inline void platram_setrw(struct platram_info *info, int to)
{
	if (info->pdata == NULL)
		return;

	if (info->pdata->set_rw != NULL)
		(info->pdata->set_rw)(info->dev, to);
}

/* platram_remove
 *
 * called to remove the device from the driver's control
*/

static int platram_remove(struct device *dev)
{
	struct platram_info *info = to_platram_info(dev);

	dev_set_drvdata(dev, NULL);

	dev_dbg(dev, "removing device\n");

	if (info == NULL)
		return 0;

	if (info->mtd) {
#ifdef CONFIG_MTD_PARTITIONS
		if (info->partitions) {
			del_mtd_partitions(info->mtd);
			kfree(info->partitions);
		}
#endif
		del_mtd_device(info->mtd);
		map_destroy(info->mtd);
	}

	/* ensure ram is left read-only */

	platram_setrw(info, PLATRAM_RO);

	/* release resources */

	if (info->area) {
		release_resource(info->area);
		kfree(info->area);
	}

	if (info->map.virt != NULL)
		iounmap(info->map.virt);

	kfree(info);

	return 0;
}

/* platram_probe
 *
 * called from device drive system when a device matching our
 * driver is found.
*/

static int platram_probe(struct device *dev)
{
	struct platform_device *pd = to_platform_device(dev);
	struct platdata_mtd_ram	*pdata;
	struct platram_info *info;
	struct resource *res;
	int err = 0;

	dev_dbg(dev, "probe entered\n");

	if (dev->platform_data == NULL) {
		dev_err(dev, "no platform data supplied\n");
		err = -ENOENT;
		goto exit_error;
	}

	pdata = dev->platform_data;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL) {
		dev_err(dev, "no memory for flash info\n");
		err = -ENOMEM;
		goto exit_error;
	}

	memset(info, 0, sizeof(*info));
	dev_set_drvdata(dev, info);

	info->dev = dev;
	info->pdata = pdata;

	/* get the resource for the memory mapping */

	res = platform_get_resource(pd, IORESOURCE_MEM, 0);

	if (res == NULL) {
		dev_err(dev, "no memory resource specified\n");
		err = -ENOENT;
		goto exit_free;
	}

	dev_dbg(dev, "got platform resource %p (0x%lx)\n", res, res->start);

	/* setup map parameters */

	info->map.phys = res->start;
	info->map.size = (res->end - res->start) + 1;
	info->map.name = pdata->mapname != NULL ? pdata->mapname : (char *)pd->name;
	info->map.bankwidth = pdata->bankwidth;

	/* register our usage of the memory area */

	info->area = request_mem_region(res->start, info->map.size, pd->name);
	if (info->area == NULL) {
		dev_err(dev, "failed to request memory region\n");
		err = -EIO;
		goto exit_free;
	}

	/* remap the memory area */

	info->map.virt = ioremap(res->start, info->map.size);
	dev_dbg(dev, "virt %p, %lu bytes\n", info->map.virt, info->map.size);

	if (info->map.virt == NULL) {
		dev_err(dev, "failed to ioremap() region\n");
		err = -EIO;
		goto exit_free;
	}

	simple_map_init(&info->map);

	dev_dbg(dev, "initialised map, probing for mtd\n");

	/* probe for the right mtd map driver */

	info->mtd = do_map_probe("map_ram" , &info->map);
	if (info->mtd == NULL) {
		dev_err(dev, "failed to probe for map_ram\n");
		err = -ENOMEM;
		goto exit_free;
	}

	info->mtd->owner = THIS_MODULE;

	platram_setrw(info, PLATRAM_RW);

	/* check to see if there are any available partitions, or wether
	 * to add this device whole */

#ifdef CONFIG_MTD_PARTITIONS
	if (pdata->nr_partitions > 0) {
		const char **probes = { NULL };

		if (pdata->probes)
			probes = (const char **)pdata->probes;

		err = parse_mtd_partitions(info->mtd, probes,
					   &info->partitions, 0);
		if (err > 0) {
			err = add_mtd_partitions(info->mtd, info->partitions,
						 err);
		}
	}
#endif /* CONFIG_MTD_PARTITIONS */

	if (add_mtd_device(info->mtd)) {
		dev_err(dev, "add_mtd_device() failed\n");
		err = -ENOMEM;
	}

	dev_info(dev, "registered mtd device\n");
	return err;

 exit_free:
	platram_remove(dev);
 exit_error:
	return err;
}

/* device driver info */

static struct device_driver platram_driver = {
	.name		= "mtd-ram",
	.owner		= THIS_MODULE,
	.bus		= &platform_bus_type,
	.probe		= platram_probe,
	.remove		= platram_remove,
};

/* module init/exit */

static int __init platram_init(void)
{
	printk("Generic platform RAM MTD, (c) 2004 Simtec Electronics\n");
	return driver_register(&platram_driver);
}

static void __exit platram_exit(void)
{
	driver_unregister(&platram_driver);
}

module_init(platram_init);
module_exit(platram_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("MTD platform RAM map driver");
