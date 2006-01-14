/*
 * Bus & driver management routines for devices within
 * a MacIO ASIC. Interface to new driver model mostly
 * stolen from the PCI version.
 * 
 *  Copyright (C) 2005 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * TODO:
 * 
 *  - Don't probe below media bay by default, but instead provide
 *    some hooks for media bay to dynamically add/remove it's own
 *    sub-devices.
 */
 
#include <linux/config.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/machdep.h>
#include <asm/macio.h>
#include <asm/pmac_feature.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#undef DEBUG

#define MAX_NODE_NAME_SIZE (BUS_ID_SIZE - 12)

static struct macio_chip      *macio_on_hold;

static int macio_bus_match(struct device *dev, struct device_driver *drv) 
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * macio_drv = to_macio_driver(drv);
	const struct of_device_id * matches = macio_drv->match_table;

	if (!matches) 
		return 0;

	return of_match_device(matches, &macio_dev->ofdev) != NULL;
}

struct macio_dev *macio_dev_get(struct macio_dev *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->ofdev.dev);
	if (tmp)
		return to_macio_device(tmp);
	else
		return NULL;
}

void macio_dev_put(struct macio_dev *dev)
{
	if (dev)
		put_device(&dev->ofdev.dev);
}


static int macio_device_probe(struct device *dev)
{
	int error = -ENODEV;
	struct macio_driver *drv;
	struct macio_dev *macio_dev;
	const struct of_device_id *match;

	drv = to_macio_driver(dev->driver);
	macio_dev = to_macio_device(dev);

	if (!drv->probe)
		return error;

	macio_dev_get(macio_dev);

	match = of_match_device(drv->match_table, &macio_dev->ofdev);
	if (match)
		error = drv->probe(macio_dev, match);
	if (error)
		macio_dev_put(macio_dev);

	return error;
}

static int macio_device_remove(struct device *dev)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv = to_macio_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(macio_dev);
	macio_dev_put(macio_dev);

	return 0;
}

static void macio_device_shutdown(struct device *dev)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv = to_macio_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(macio_dev);
}

static int macio_device_suspend(struct device *dev, pm_message_t state)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv = to_macio_driver(dev->driver);

	if (dev->driver && drv->suspend)
		return drv->suspend(macio_dev, state);
	return 0;
}

static int macio_device_resume(struct device * dev)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv = to_macio_driver(dev->driver);

	if (dev->driver && drv->resume)
		return drv->resume(macio_dev);
	return 0;
}

static int macio_uevent(struct device *dev, char **envp, int num_envp,
                          char *buffer, int buffer_size)
{
	struct macio_dev * macio_dev;
	struct of_device * of;
	char *scratch, *compat;
	int i = 0;
	int length = 0;
	int cplen, seen = 0;

	if (!dev)
		return -ENODEV;

	macio_dev = to_macio_device(dev);
	if (!macio_dev)
		return -ENODEV;

	of = &macio_dev->ofdev;
	scratch = buffer;

	/* stuff we want to pass to /sbin/hotplug */
	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "OF_NAME=%s",
	                     of->node->name);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "OF_TYPE=%s",
	                     of->node->type);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

        /* Since the compatible field can contain pretty much anything
         * it's not really legal to split it out with commas. We split it
         * up using a number of environment variables instead. */

	compat = (char *) get_property(of->node, "compatible", &cplen);
	while (compat && cplen > 0) {
		int l;
                envp[i++] = scratch;
		length += scnprintf (scratch, buffer_size - length,
		                     "OF_COMPATIBLE_%d=%s", seen, compat);
		if ((buffer_size - length <= 0) || (i >= num_envp))
			return -ENOMEM;
		length++;
		scratch += length;
		l = strlen (compat) + 1;
		compat += l;
		cplen -= l;
		seen++;
	}

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length,
	                     "OF_COMPATIBLE_N=%d", seen);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i] = NULL;

	return 0;
}

extern struct device_attribute macio_dev_attrs[];

struct bus_type macio_bus_type = {
       .name	= "macio",
       .match	= macio_bus_match,
       .uevent = macio_uevent,
       .probe	= macio_device_probe,
       .remove	= macio_device_remove,
       .shutdown = macio_device_shutdown,
       .suspend	= macio_device_suspend,
       .resume	= macio_device_resume,
       .dev_attrs = macio_dev_attrs,
};

static int __init macio_bus_driver_init(void)
{
	return bus_register(&macio_bus_type);
}

postcore_initcall(macio_bus_driver_init);


/**
 * macio_release_dev - free a macio device structure when all users of it are
 * finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this macio device
 * are done. This currently means never as we don't hot remove any macio
 * device yet, though that will happen with mediabay based devices in a later
 * implementation.
 */
static void macio_release_dev(struct device *dev)
{
	struct macio_dev *mdev;

        mdev = to_macio_device(dev);
	kfree(mdev);
}

/**
 * macio_resource_quirks - tweak or skip some resources for a device
 * @np: pointer to the device node
 * @res: resulting resource
 * @index: index of resource in node
 *
 * If this routine returns non-null, then the resource is completely
 * skipped.
 */
static int macio_resource_quirks(struct device_node *np, struct resource *res,
				 int index)
{
	if (res->flags & IORESOURCE_MEM) {
		/* Grand Central has too large resource 0 on some machines */
		if (index == 0 && !strcmp(np->name, "gc"))
			res->end = res->start + 0x1ffff;

		/* Airport has bogus resource 2 */
		if (index >= 2 && !strcmp(np->name, "radio"))
			return 1;

#ifndef CONFIG_PPC64
		/* DBDMAs may have bogus sizes */
		if ((res->start & 0x0001f000) == 0x00008000)
			res->end = res->start + 0xff;
#endif /* CONFIG_PPC64 */

		/* ESCC parent eats child resources. We could have added a
		 * level of hierarchy, but I don't really feel the need
		 * for it
		 */
		if (!strcmp(np->name, "escc"))
			return 1;

		/* ESCC has bogus resources >= 3 */
		if (index >= 3 && !(strcmp(np->name, "ch-a") &&
				    strcmp(np->name, "ch-b")))
			return 1;

		/* Media bay has too many resources, keep only first one */
		if (index > 0 && !strcmp(np->name, "media-bay"))
			return 1;

		/* Some older IDE resources have bogus sizes */
		if (!(strcmp(np->name, "IDE") && strcmp(np->name, "ATA") &&
		      strcmp(np->type, "ide") && strcmp(np->type, "ata"))) {
			if (index == 0 && (res->end - res->start) > 0xfff)
				res->end = res->start + 0xfff;
			if (index == 1 && (res->end - res->start) > 0xff)
				res->end = res->start + 0xff;
		}
	}
	return 0;
}


static void macio_setup_interrupts(struct macio_dev *dev)
{
	struct device_node *np = dev->ofdev.node;
	int i,j;

	/* For now, we use pre-parsed entries in the device-tree for
	 * interrupt routing and addresses, but we should change that
	 * to dynamically parsed entries and so get rid of most of the
	 * clutter in struct device_node
	 */
	for (i = j = 0; i < np->n_intrs; i++) {
		struct resource *res = &dev->interrupt[j];

		if (j >= MACIO_DEV_COUNT_IRQS)
			break;
		res->start = np->intrs[i].line;
		res->flags = IORESOURCE_IO;
		if (np->intrs[j].sense)
			res->flags |= IORESOURCE_IRQ_LOWLEVEL;
		else
			res->flags |= IORESOURCE_IRQ_HIGHEDGE;
		res->name = dev->ofdev.dev.bus_id;
		if (macio_resource_quirks(np, res, i))
			memset(res, 0, sizeof(struct resource));
		else
			j++;
	}
	dev->n_interrupts = j;
}

static void macio_setup_resources(struct macio_dev *dev,
				  struct resource *parent_res)
{
	struct device_node *np = dev->ofdev.node;
	struct resource r;
	int index;

	for (index = 0; of_address_to_resource(np, index, &r) == 0; index++) {
		struct resource *res = &dev->resource[index];
		if (index >= MACIO_DEV_COUNT_RESOURCES)
			break;
		*res = r;
		res->name = dev->ofdev.dev.bus_id;

		if (macio_resource_quirks(np, res, index)) {
			memset(res, 0, sizeof(struct resource));
			continue;
		}
		/* Currently, we consider failure as harmless, this may
		 * change in the future, once I've found all the device
		 * tree bugs in older machines & worked around them
		 */
		if (insert_resource(parent_res, res)) {
			printk(KERN_WARNING "Can't request resource "
			       "%d for MacIO device %s\n",
			       index, dev->ofdev.dev.bus_id);
		}
	}
	dev->n_resources = index;
}

/**
 * macio_add_one_device - Add one device from OF node to the device tree
 * @chip: pointer to the macio_chip holding the device
 * @np: pointer to the device node in the OF tree
 * @in_bay: set to 1 if device is part of a media-bay
 *
 * When media-bay is changed to hotswap drivers, this function will
 * be exposed to the bay driver some way...
 */
static struct macio_dev * macio_add_one_device(struct macio_chip *chip,
					       struct device *parent,
					       struct device_node *np,
					       struct macio_dev *in_bay,
					       struct resource *parent_res)
{
	struct macio_dev *dev;
	u32 *reg;
	
	if (np == NULL)
		return NULL;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(*dev));

	dev->bus = &chip->lbus;
	dev->media_bay = in_bay;
	dev->ofdev.node = np;
	dev->ofdev.dma_mask = 0xffffffffUL;
	dev->ofdev.dev.dma_mask = &dev->ofdev.dma_mask;
	dev->ofdev.dev.parent = parent;
	dev->ofdev.dev.bus = &macio_bus_type;
	dev->ofdev.dev.release = macio_release_dev;

#ifdef DEBUG
	printk("preparing mdev @%p, ofdev @%p, dev @%p, kobj @%p\n",
	       dev, &dev->ofdev, &dev->ofdev.dev, &dev->ofdev.dev.kobj);
#endif

	/* MacIO itself has a different reg, we use it's PCI base */
	if (np == chip->of_node) {
		sprintf(dev->ofdev.dev.bus_id, "%1d.%08lx:%.*s",
			chip->lbus.index,
#ifdef CONFIG_PCI
			pci_resource_start(chip->lbus.pdev, 0),
#else
			0, /* NuBus may want to do something better here */
#endif
			MAX_NODE_NAME_SIZE, np->name);
	} else {
		reg = (u32 *)get_property(np, "reg", NULL);
		sprintf(dev->ofdev.dev.bus_id, "%1d.%08x:%.*s",
			chip->lbus.index,
			reg ? *reg : 0, MAX_NODE_NAME_SIZE, np->name);
	}

	/* Setup interrupts & resources */
	macio_setup_interrupts(dev);
	macio_setup_resources(dev, parent_res);

	/* Register with core */
	if (of_device_register(&dev->ofdev) != 0) {
		printk(KERN_DEBUG"macio: device registration error for %s!\n",
		       dev->ofdev.dev.bus_id);
		kfree(dev);
		return NULL;
	}

	return dev;
}

static int macio_skip_device(struct device_node *np)
{
	if (strncmp(np->name, "battery", 7) == 0)
		return 1;
	if (strncmp(np->name, "escc-legacy", 11) == 0)
		return 1;
	return 0;
}

/**
 * macio_pci_add_devices - Adds sub-devices of mac-io to the device tree
 * @chip: pointer to the macio_chip holding the devices
 * 
 * This function will do the job of extracting devices from the
 * Open Firmware device tree, build macio_dev structures and add
 * them to the Linux device tree.
 * 
 * For now, childs of media-bay are added now as well. This will
 * change rsn though.
 */
static void macio_pci_add_devices(struct macio_chip *chip)
{
	struct device_node *np, *pnode;
	struct macio_dev *rdev, *mdev, *mbdev = NULL, *sdev = NULL;
	struct device *parent = NULL;
	struct resource *root_res = &iomem_resource;
	
	/* Add a node for the macio bus itself */
#ifdef CONFIG_PCI
	if (chip->lbus.pdev) {
		parent = &chip->lbus.pdev->dev;
		root_res = &chip->lbus.pdev->resource[0];
	}
#endif
	pnode = of_node_get(chip->of_node);
	if (pnode == NULL)
		return;

	/* Add macio itself to hierarchy */
	rdev = macio_add_one_device(chip, parent, pnode, NULL, root_res);
	if (rdev == NULL)
		return;
	root_res = &rdev->resource[0];

	/* First scan 1st level */
	for (np = NULL; (np = of_get_next_child(pnode, np)) != NULL;) {
		if (macio_skip_device(np))
			continue;
		of_node_get(np);
		mdev = macio_add_one_device(chip, &rdev->ofdev.dev, np, NULL,
					    root_res);
		if (mdev == NULL)
			of_node_put(np);
		else if (strncmp(np->name, "media-bay", 9) == 0)
			mbdev = mdev;
		else if (strncmp(np->name, "escc", 4) == 0)
			sdev = mdev;
	}

	/* Add media bay devices if any */
	if (mbdev)
		for (np = NULL; (np = of_get_next_child(mbdev->ofdev.node, np))
			     != NULL;) {
			if (macio_skip_device(np))
				continue;
			of_node_get(np);
			if (macio_add_one_device(chip, &mbdev->ofdev.dev, np,
						 mbdev,  root_res) == NULL)
				of_node_put(np);
		}

	/* Add serial ports if any */
	if (sdev) {
		for (np = NULL; (np = of_get_next_child(sdev->ofdev.node, np))
			     != NULL;) {
			if (macio_skip_device(np))
				continue;
			of_node_get(np);
			if (macio_add_one_device(chip, &sdev->ofdev.dev, np,
						 NULL, root_res) == NULL)
				of_node_put(np);
		}
	}
}


/**
 * macio_register_driver - Registers a new MacIO device driver
 * @drv: pointer to the driver definition structure
 */
int macio_register_driver(struct macio_driver *drv)
{
	int count = 0;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &macio_bus_type;

	/* register with core */
	count = driver_register(&drv->driver);
	return count ? count : 1;
}

/**
 * macio_unregister_driver - Unregisters a new MacIO device driver
 * @drv: pointer to the driver definition structure
 */
void macio_unregister_driver(struct macio_driver *drv)
{
	driver_unregister(&drv->driver);
}

/**
 *	macio_request_resource - Request an MMIO resource
 * 	@dev: pointer to the device holding the resource
 *	@resource_no: resource number to request
 *	@name: resource name
 *
 *	Mark  memory region number @resource_no associated with MacIO
 *	device @dev as being reserved by owner @name.  Do not access
 *	any address inside the memory regions unless this call returns
 *	successfully.
 *
 *	Returns 0 on success, or %EBUSY on error.  A warning
 *	message is also printed on failure.
 */
int macio_request_resource(struct macio_dev *dev, int resource_no,
			   const char *name)
{
	if (macio_resource_len(dev, resource_no) == 0)
		return 0;
		
	if (!request_mem_region(macio_resource_start(dev, resource_no),
				macio_resource_len(dev, resource_no),
				name))
		goto err_out;
	
	return 0;

err_out:
	printk (KERN_WARNING "MacIO: Unable to reserve resource #%d:%lx@%lx"
		" for device %s\n",
		resource_no,
		macio_resource_len(dev, resource_no),
		macio_resource_start(dev, resource_no),
		dev->ofdev.dev.bus_id);
	return -EBUSY;
}

/**
 * macio_release_resource - Release an MMIO resource
 * @dev: pointer to the device holding the resource
 * @resource_no: resource number to release
 */
void macio_release_resource(struct macio_dev *dev, int resource_no)
{
	if (macio_resource_len(dev, resource_no) == 0)
		return;
	release_mem_region(macio_resource_start(dev, resource_no),
			   macio_resource_len(dev, resource_no));
}

/**
 *	macio_request_resources - Reserve all memory resources
 *	@dev: MacIO device whose resources are to be reserved
 *	@name: Name to be associated with resource.
 *
 *	Mark all memory regions associated with MacIO device @dev as
 *	being reserved by owner @name.  Do not access any address inside
 *	the memory regions unless this call returns successfully.
 *
 *	Returns 0 on success, or %EBUSY on error.  A warning
 *	message is also printed on failure.
 */
int macio_request_resources(struct macio_dev *dev, const char *name)
{
	int i;
	
	for (i = 0; i < dev->n_resources; i++)
		if (macio_request_resource(dev, i, name))
			goto err_out;
	return 0;

err_out:
	while(--i >= 0)
		macio_release_resource(dev, i);
		
	return -EBUSY;
}

/**
 *	macio_release_resources - Release reserved memory resources
 *	@dev: MacIO device whose resources were previously reserved
 */

void macio_release_resources(struct macio_dev *dev)
{
	int i;
	
	for (i = 0; i < dev->n_resources; i++)
		macio_release_resource(dev, i);
}


#ifdef CONFIG_PCI

static int __devinit macio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device_node* np;
	struct macio_chip* chip;
	
	if (ent->vendor != PCI_VENDOR_ID_APPLE)
		return -ENODEV;

	/* Note regarding refcounting: We assume pci_device_to_OF_node() is
	 * ported to new OF APIs and returns a node with refcount incremented.
	 */
	np = pci_device_to_OF_node(pdev);
	if (np == NULL)
		return -ENODEV;

	/* The above assumption is wrong !!!
	 * fix that here for now until I fix the arch code
	 */
	of_node_get(np);

	/* We also assume that pmac_feature will have done a get() on nodes
	 * stored in the macio chips array
	 */
	chip = macio_find(np, macio_unknown);
       	of_node_put(np);
	if (chip == NULL)
		return -ENODEV;

	/* XXX Need locking ??? */
	if (chip->lbus.pdev == NULL) {
		chip->lbus.pdev = pdev;
		chip->lbus.chip = chip;
		pci_set_drvdata(pdev, &chip->lbus);
		pci_set_master(pdev);
	}

	printk(KERN_INFO "MacIO PCI driver attached to %s chipset\n",
		chip->name);

	/*
	 * HACK ALERT: The WallStreet PowerBook and some OHare based machines
	 * have 2 macio ASICs. I must probe the "main" one first or IDE
	 * ordering will be incorrect. So I put on "hold" the second one since
	 * it seem to appear first on PCI
	 */
	if (chip->type == macio_gatwick || chip->type == macio_ohareII)
		if (macio_chips[0].lbus.pdev == NULL) {
			macio_on_hold = chip;
			return 0;
		}

	macio_pci_add_devices(chip);
	if (macio_on_hold && macio_chips[0].lbus.pdev != NULL) {
		macio_pci_add_devices(macio_on_hold);
		macio_on_hold = NULL;
	}

	return 0;
}

static void __devexit macio_pci_remove(struct pci_dev* pdev)
{
	panic("removing of macio-asic not supported !\n");
}

/*
 * MacIO is matched against any Apple ID, it's probe() function
 * will then decide wether it applies or not
 */
static const struct pci_device_id __devinitdata pci_ids [] = { {
	.vendor		= PCI_VENDOR_ID_APPLE,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,

	}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver macio_pci_driver = {
	.name		= (char *) "macio",
	.id_table	= pci_ids,

	.probe		= macio_pci_probe,
	.remove		= macio_pci_remove,
};

#endif /* CONFIG_PCI */

static int __init macio_module_init (void) 
{
#ifdef CONFIG_PCI
	int rc;

	rc = pci_register_driver(&macio_pci_driver);
	if (rc)
		return rc;
#endif /* CONFIG_PCI */
	return 0;
}

module_init(macio_module_init);

EXPORT_SYMBOL(macio_register_driver);
EXPORT_SYMBOL(macio_unregister_driver);
EXPORT_SYMBOL(macio_dev_get);
EXPORT_SYMBOL(macio_dev_put);
EXPORT_SYMBOL(macio_request_resource);
EXPORT_SYMBOL(macio_release_resource);
EXPORT_SYMBOL(macio_request_resources);
EXPORT_SYMBOL(macio_release_resources);
