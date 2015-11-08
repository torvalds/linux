/*
 *  Copyright (C) 1994-1998	    Linus Torvalds & authors (see below)
 *  Copyright (C) 2003-2005, 2007   Bartlomiej Zolnierkiewicz
 */

/*
 *  Mostly written by Mark Lord  <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to MAX_HWIFS IDE interfaces, on one or more IRQs
 *   (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * ...
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@pobox.com).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@pobox.com)		(IDE Perf.Pkg)
 *	Delman Lee	(delman@ieee.org)		("Mr. atdisk2")
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/hdreg.h>
#include <linux/completion.h>
#include <linux/device.h>

struct class *ide_port_class;

/**
 * ide_device_get	-	get an additional reference to a ide_drive_t
 * @drive:	device to get a reference to
 *
 * Gets a reference to the ide_drive_t and increments the use count of the
 * underlying LLDD module.
 */
int ide_device_get(ide_drive_t *drive)
{
	struct device *host_dev;
	struct module *module;

	if (!get_device(&drive->gendev))
		return -ENXIO;

	host_dev = drive->hwif->host->dev[0];
	module = host_dev ? host_dev->driver->owner : NULL;

	if (module && !try_module_get(module)) {
		put_device(&drive->gendev);
		return -ENXIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ide_device_get);

/**
 * ide_device_put	-	release a reference to a ide_drive_t
 * @drive:	device to release a reference on
 *
 * Release a reference to the ide_drive_t and decrements the use count of
 * the underlying LLDD module.
 */
void ide_device_put(ide_drive_t *drive)
{
#ifdef CONFIG_MODULE_UNLOAD
	struct device *host_dev = drive->hwif->host->dev[0];
	struct module *module = host_dev ? host_dev->driver->owner : NULL;

	module_put(module);
#endif
	put_device(&drive->gendev);
}
EXPORT_SYMBOL_GPL(ide_device_put);

static int ide_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int ide_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	ide_drive_t *drive = to_ide_device(dev);

	add_uevent_var(env, "MEDIA=%s", ide_media_string(drive));
	add_uevent_var(env, "DRIVENAME=%s", drive->name);
	add_uevent_var(env, "MODALIAS=ide:m-%s", ide_media_string(drive));
	return 0;
}

static int generic_ide_probe(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	struct ide_driver *drv = to_ide_driver(dev->driver);

	return drv->probe ? drv->probe(drive) : -ENODEV;
}

static int generic_ide_remove(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	struct ide_driver *drv = to_ide_driver(dev->driver);

	if (drv->remove)
		drv->remove(drive);

	return 0;
}

static void generic_ide_shutdown(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	struct ide_driver *drv = to_ide_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(drive);
}

struct bus_type ide_bus_type = {
	.name		= "ide",
	.match		= ide_bus_match,
	.uevent		= ide_uevent,
	.probe		= generic_ide_probe,
	.remove		= generic_ide_remove,
	.shutdown	= generic_ide_shutdown,
	.dev_groups	= ide_dev_groups,
	.suspend	= generic_ide_suspend,
	.resume		= generic_ide_resume,
};

EXPORT_SYMBOL_GPL(ide_bus_type);

int ide_vlb_clk;
EXPORT_SYMBOL_GPL(ide_vlb_clk);

module_param_named(vlb_clock, ide_vlb_clk, int, 0);
MODULE_PARM_DESC(vlb_clock, "VLB clock frequency (in MHz)");

int ide_pci_clk;
EXPORT_SYMBOL_GPL(ide_pci_clk);

module_param_named(pci_clock, ide_pci_clk, int, 0);
MODULE_PARM_DESC(pci_clock, "PCI bus clock frequency (in MHz)");

static int ide_set_dev_param_mask(const char *s, const struct kernel_param *kp)
{
	int a, b, i, j = 1;
	unsigned int *dev_param_mask = (unsigned int *)kp->arg;

	/* controller . device (0 or 1) [ : 1 (set) | 0 (clear) ] */
	if (sscanf(s, "%d.%d:%d", &a, &b, &j) != 3 &&
	    sscanf(s, "%d.%d", &a, &b) != 2)
		return -EINVAL;

	i = a * MAX_DRIVES + b;

	if (i >= MAX_HWIFS * MAX_DRIVES || j < 0 || j > 1)
		return -EINVAL;

	if (j)
		*dev_param_mask |= (1 << i);
	else
		*dev_param_mask &= ~(1 << i);

	return 0;
}

static const struct kernel_param_ops param_ops_ide_dev_mask = {
	.set = ide_set_dev_param_mask
};

#define param_check_ide_dev_mask(name, p) param_check_uint(name, p)

static unsigned int ide_nodma;

module_param_named(nodma, ide_nodma, ide_dev_mask, 0);
MODULE_PARM_DESC(nodma, "disallow DMA for a device");

static unsigned int ide_noflush;

module_param_named(noflush, ide_noflush, ide_dev_mask, 0);
MODULE_PARM_DESC(noflush, "disable flush requests for a device");

static unsigned int ide_nohpa;

module_param_named(nohpa, ide_nohpa, ide_dev_mask, 0);
MODULE_PARM_DESC(nohpa, "disable Host Protected Area for a device");

static unsigned int ide_noprobe;

module_param_named(noprobe, ide_noprobe, ide_dev_mask, 0);
MODULE_PARM_DESC(noprobe, "skip probing for a device");

static unsigned int ide_nowerr;

module_param_named(nowerr, ide_nowerr, ide_dev_mask, 0);
MODULE_PARM_DESC(nowerr, "ignore the ATA_DF bit for a device");

static unsigned int ide_cdroms;

module_param_named(cdrom, ide_cdroms, ide_dev_mask, 0);
MODULE_PARM_DESC(cdrom, "force device as a CD-ROM");

struct chs_geom {
	unsigned int	cyl;
	u8		head;
	u8		sect;
};

static unsigned int ide_disks;
static struct chs_geom ide_disks_chs[MAX_HWIFS * MAX_DRIVES];

static int ide_set_disk_chs(const char *str, struct kernel_param *kp)
{
	int a, b, c = 0, h = 0, s = 0, i, j = 1;

	/* controller . device (0 or 1) : Cylinders , Heads , Sectors */
	/* controller . device (0 or 1) : 1 (use CHS) | 0 (ignore CHS) */
	if (sscanf(str, "%d.%d:%d,%d,%d", &a, &b, &c, &h, &s) != 5 &&
	    sscanf(str, "%d.%d:%d", &a, &b, &j) != 3)
		return -EINVAL;

	i = a * MAX_DRIVES + b;

	if (i >= MAX_HWIFS * MAX_DRIVES || j < 0 || j > 1)
		return -EINVAL;

	if (c > INT_MAX || h > 255 || s > 255)
		return -EINVAL;

	if (j)
		ide_disks |= (1 << i);
	else
		ide_disks &= ~(1 << i);

	ide_disks_chs[i].cyl  = c;
	ide_disks_chs[i].head = h;
	ide_disks_chs[i].sect = s;

	return 0;
}

module_param_call(chs, ide_set_disk_chs, NULL, NULL, 0);
MODULE_PARM_DESC(chs, "force device as a disk (using CHS)");

static void ide_dev_apply_params(ide_drive_t *drive, u8 unit)
{
	int i = drive->hwif->index * MAX_DRIVES + unit;

	if (ide_nodma & (1 << i)) {
		printk(KERN_INFO "ide: disallowing DMA for %s\n", drive->name);
		drive->dev_flags |= IDE_DFLAG_NODMA;
	}
	if (ide_noflush & (1 << i)) {
		printk(KERN_INFO "ide: disabling flush requests for %s\n",
				 drive->name);
		drive->dev_flags |= IDE_DFLAG_NOFLUSH;
	}
	if (ide_nohpa & (1 << i)) {
		printk(KERN_INFO "ide: disabling Host Protected Area for %s\n",
				 drive->name);
		drive->dev_flags |= IDE_DFLAG_NOHPA;
	}
	if (ide_noprobe & (1 << i)) {
		printk(KERN_INFO "ide: skipping probe for %s\n", drive->name);
		drive->dev_flags |= IDE_DFLAG_NOPROBE;
	}
	if (ide_nowerr & (1 << i)) {
		printk(KERN_INFO "ide: ignoring the ATA_DF bit for %s\n",
				 drive->name);
		drive->bad_wstat = BAD_R_STAT;
	}
	if (ide_cdroms & (1 << i)) {
		printk(KERN_INFO "ide: forcing %s as a CD-ROM\n", drive->name);
		drive->dev_flags |= IDE_DFLAG_PRESENT;
		drive->media = ide_cdrom;
		/* an ATAPI device ignores DRDY */
		drive->ready_stat = 0;
	}
	if (ide_disks & (1 << i)) {
		drive->cyl  = drive->bios_cyl  = ide_disks_chs[i].cyl;
		drive->head = drive->bios_head = ide_disks_chs[i].head;
		drive->sect = drive->bios_sect = ide_disks_chs[i].sect;

		printk(KERN_INFO "ide: forcing %s as a disk (%d/%d/%d)\n",
				 drive->name,
				 drive->cyl, drive->head, drive->sect);

		drive->dev_flags |= IDE_DFLAG_FORCED_GEOM | IDE_DFLAG_PRESENT;
		drive->media = ide_disk;
		drive->ready_stat = ATA_DRDY;
	}
}

static unsigned int ide_ignore_cable;

static int ide_set_ignore_cable(const char *s, struct kernel_param *kp)
{
	int i, j = 1;

	/* controller (ignore) */
	/* controller : 1 (ignore) | 0 (use) */
	if (sscanf(s, "%d:%d", &i, &j) != 2 && sscanf(s, "%d", &i) != 1)
		return -EINVAL;

	if (i >= MAX_HWIFS || j < 0 || j > 1)
		return -EINVAL;

	if (j)
		ide_ignore_cable |= (1 << i);
	else
		ide_ignore_cable &= ~(1 << i);

	return 0;
}

module_param_call(ignore_cable, ide_set_ignore_cable, NULL, NULL, 0);
MODULE_PARM_DESC(ignore_cable, "ignore cable detection");

void ide_port_apply_params(ide_hwif_t *hwif)
{
	ide_drive_t *drive;
	int i;

	if (ide_ignore_cable & (1 << hwif->index)) {
		printk(KERN_INFO "ide: ignoring cable detection for %s\n",
				 hwif->name);
		hwif->cbl = ATA_CBL_PATA40_SHORT;
	}

	ide_port_for_each_dev(i, drive, hwif)
		ide_dev_apply_params(drive, i);
}

/*
 * This is gets invoked once during initialization, to set *everything* up
 */
static int __init ide_init(void)
{
	int ret;

	printk(KERN_INFO "Uniform Multi-Platform E-IDE driver\n");

	ret = bus_register(&ide_bus_type);
	if (ret < 0) {
		printk(KERN_WARNING "IDE: bus_register error: %d\n", ret);
		return ret;
	}

	ide_port_class = class_create(THIS_MODULE, "ide_port");
	if (IS_ERR(ide_port_class)) {
		ret = PTR_ERR(ide_port_class);
		goto out_port_class;
	}

	ide_acpi_init();

	proc_ide_create();

	return 0;

out_port_class:
	bus_unregister(&ide_bus_type);

	return ret;
}

static void __exit ide_exit(void)
{
	proc_ide_destroy();

	class_destroy(ide_port_class);

	bus_unregister(&ide_bus_type);
}

module_init(ide_init);
module_exit(ide_exit);

MODULE_LICENSE("GPL");
