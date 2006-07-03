/* $Id: sun_uflash.c,v 1.13 2005/11/07 11:14:28 gleixner Exp $
 *
 * sun_uflash - Driver implementation for user-programmable flash
 * present on many Sun Microsystems SME boardsets.
 *
 * This driver does NOT provide access to the OBP-flash for
 * safety reasons-- use <linux>/drivers/sbus/char/flash.c instead.
 *
 * Copyright (c) 2001 Eric Brower (ebrower@usa.net)
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

#define UFLASH_OBPNAME	"flashprom"
#define UFLASH_DEVNAME 	"userflash"

#define UFLASH_WINDOW_SIZE	0x200000
#define UFLASH_BUSWIDTH		1			/* EBus is 8-bit */

MODULE_AUTHOR("Eric Brower <ebrower@usa.net>");
MODULE_DESCRIPTION("User-programmable flash device on Sun Microsystems boardsets");
MODULE_SUPPORTED_DEVICE("userflash");
MODULE_LICENSE("GPL");
MODULE_VERSION("2.0");

static LIST_HEAD(device_list);
struct uflash_dev {
	char			*name;	/* device name */
	struct map_info 	map;	/* mtd map info */
	struct mtd_info		*mtd;	/* mtd info */
};


struct map_info uflash_map_templ = {
	.name =		"SUNW,???-????",
	.size =		UFLASH_WINDOW_SIZE,
	.bankwidth =	UFLASH_BUSWIDTH,
};

int uflash_devinit(struct linux_ebus_device *edev, struct device_node *dp)
{
	struct uflash_dev *up;
	struct resource *res;

	res = &edev->resource[0];

	if (edev->num_addrs != 1) {
		/* Non-CFI userflash device-- once I find one we
		 * can work on supporting it.
		 */
		printk("%s: unsupported device at 0x%llx (%d regs): " \
			"email ebrower@usa.net\n",
		       dp->full_name, (unsigned long long)res->start,
		       edev->num_addrs);

		return -ENODEV;
	}

	up = kzalloc(sizeof(struct uflash_dev), GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	/* copy defaults and tweak parameters */
	memcpy(&up->map, &uflash_map_templ, sizeof(uflash_map_templ));
	up->map.size = (res->end - res->start) + 1UL;

	up->name = of_get_property(dp, "model", NULL);
	if (up->name && 0 < strlen(up->name))
		up->map.name = up->name;

	up->map.phys = res->start;

	up->map.virt = ioremap_nocache(res->start, up->map.size);
	if (!up->map.virt) {
		printk("%s: Failed to map device.\n", dp->full_name);
		kfree(up);

		return -EINVAL;
	}

	simple_map_init(&up->map);

	/* MTD registration */
	up->mtd = do_map_probe("cfi_probe", &up->map);
	if (!up->mtd) {
		iounmap(up->map.virt);
		kfree(up);

		return -ENXIO;
	}

	up->mtd->owner = THIS_MODULE;

	add_mtd_device(up->mtd);

	dev_set_drvdata(&edev->ofdev.dev, up);

	return 0;
}

static int __devinit uflash_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct linux_ebus_device *edev = to_ebus_device(&dev->dev);
	struct device_node *dp = dev->node;

	if (of_find_property(dp, "user", NULL))
		return -ENODEV;

	return uflash_devinit(edev, dp);
}

static int __devexit uflash_remove(struct of_device *dev)
{
	struct uflash_dev *up = dev_get_drvdata(&dev->dev);

	if (up->mtd) {
		del_mtd_device(up->mtd);
		map_destroy(up->mtd);
	}
	if (up->map.virt) {
		iounmap(up->map.virt);
		up->map.virt = NULL;
	}

	kfree(up);

	return 0;
}

static struct of_device_id uflash_match[] = {
	{
		.name = UFLASH_OBPNAME,
	},
	{},
};

MODULE_DEVICE_TABLE(of, uflash_match);

static struct of_platform_driver uflash_driver = {
	.name		= UFLASH_DEVNAME,
	.match_table	= uflash_match,
	.probe		= uflash_probe,
	.remove		= __devexit_p(uflash_remove),
};

static int __init uflash_init(void)
{
	return of_register_driver(&uflash_driver, &ebus_bus_type);
}

static void __exit uflash_exit(void)
{
	of_unregister_driver(&uflash_driver);
}

module_init(uflash_init);
module_exit(uflash_exit);
