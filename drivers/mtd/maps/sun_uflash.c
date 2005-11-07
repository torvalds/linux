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
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

#define UFLASH_OBPNAME	"flashprom"
#define UFLASH_DEVNAME 	"userflash"

#define UFLASH_WINDOW_SIZE	0x200000
#define UFLASH_BUSWIDTH		1			/* EBus is 8-bit */

MODULE_AUTHOR
	("Eric Brower <ebrower@usa.net>");
MODULE_DESCRIPTION
	("User-programmable flash device on Sun Microsystems boardsets");
MODULE_SUPPORTED_DEVICE
	("userflash");
MODULE_LICENSE
	("GPL");

static LIST_HEAD(device_list);
struct uflash_dev {
	char *			name;	/* device name */
	struct map_info 	map;	/* mtd map info */
	struct mtd_info *	mtd;	/* mtd info */
	struct list_head	list;
};


struct map_info uflash_map_templ = {
		.name =		"SUNW,???-????",
		.size =		UFLASH_WINDOW_SIZE,
		.bankwidth =	UFLASH_BUSWIDTH,
};

int uflash_devinit(struct linux_ebus_device* edev)
{
	int iTmp, nregs;
	struct linux_prom_registers regs[2];
	struct uflash_dev *pdev;

	iTmp = prom_getproperty(
		edev->prom_node, "reg", (void *)regs, sizeof(regs));
	if ((iTmp % sizeof(regs[0])) != 0) {
		printk("%s: Strange reg property size %d\n",
			UFLASH_DEVNAME, iTmp);
		return -ENODEV;
	}

	nregs = iTmp / sizeof(regs[0]);

	if (nregs != 1) {
		/* Non-CFI userflash device-- once I find one we
		 * can work on supporting it.
		 */
		printk("%s: unsupported device at 0x%lx (%d regs): " \
			"email ebrower@usa.net\n",
			UFLASH_DEVNAME, edev->resource[0].start, nregs);
		return -ENODEV;
	}

	if(0 == (pdev = kmalloc(sizeof(struct uflash_dev), GFP_KERNEL))) {
		printk("%s: unable to kmalloc new device\n", UFLASH_DEVNAME);
		return(-ENOMEM);
	}

	/* copy defaults and tweak parameters */
	memcpy(&pdev->map, &uflash_map_templ, sizeof(uflash_map_templ));
	pdev->map.size = regs[0].reg_size;

	iTmp = prom_getproplen(edev->prom_node, "model");
	pdev->name = kmalloc(iTmp, GFP_KERNEL);
	prom_getstring(edev->prom_node, "model", pdev->name, iTmp);
	if(0 != pdev->name && 0 < strlen(pdev->name)) {
		pdev->map.name = pdev->name;
	}
	pdev->map.phys = edev->resource[0].start;
	pdev->map.virt = ioremap_nocache(edev->resource[0].start, pdev->map.size);
	if(0 == pdev->map.virt) {
		printk("%s: failed to map device\n", __FUNCTION__);
		kfree(pdev->name);
		kfree(pdev);
		return(-1);
	}

	simple_map_init(&pdev->map);

	/* MTD registration */
	pdev->mtd = do_map_probe("cfi_probe", &pdev->map);
	if(0 == pdev->mtd) {
		iounmap(pdev->map.virt);
		kfree(pdev->name);
		kfree(pdev);
		return(-ENXIO);
	}

	list_add(&pdev->list, &device_list);

	pdev->mtd->owner = THIS_MODULE;

	add_mtd_device(pdev->mtd);
	return(0);
}

static int __init uflash_init(void)
{
	struct linux_ebus *ebus = NULL;
	struct linux_ebus_device *edev = NULL;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, UFLASH_OBPNAME)) {
				if(0 > prom_getproplen(edev->prom_node, "user")) {
					DEBUG(2, "%s: ignoring device at 0x%lx\n",
							UFLASH_DEVNAME, edev->resource[0].start);
				} else {
					uflash_devinit(edev);
				}
			}
		}
	}

	if(list_empty(&device_list)) {
		printk("%s: unable to locate device\n", UFLASH_DEVNAME);
		return -ENODEV;
	}
	return(0);
}

static void __exit uflash_cleanup(void)
{
	struct list_head *udevlist;
	struct uflash_dev *udev;

	list_for_each(udevlist, &device_list) {
		udev = list_entry(udevlist, struct uflash_dev, list);
		DEBUG(2, "%s: removing device %s\n",
			UFLASH_DEVNAME, udev->name);

		if(0 != udev->mtd) {
			del_mtd_device(udev->mtd);
			map_destroy(udev->mtd);
		}
		if(0 != udev->map.virt) {
			iounmap(udev->map.virt);
			udev->map.virt = NULL;
		}
		kfree(udev->name);
		kfree(udev);
	}
}

module_init(uflash_init);
module_exit(uflash_cleanup);
