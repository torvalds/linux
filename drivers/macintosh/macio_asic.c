// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Bus & driver management routines for devices within
 * a MacIO ASIC. Interface to new driver model mostly
 * stolen from the PCI version.
 * 
 *  Copyright (C) 2005 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 * TODO:
 * 
 *  - Don't probe below media bay by default, but instead provide
 *    some hooks for media bay to dynamically add/remove it's own
 *    sub-devices.
 */
 
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>

#include <asm/machdep.h>
#include <asm/macio.h>
#include <asm/pmac_feature.h>

#undef DEBUG

#define MAX_NODE_NAME_SIZE (20 - 12)

static struct macio_chip      *macio_on_hold;

static int macio_bus_match(struct device *dev, struct device_driver *drv) 
{
	const struct of_device_id * matches = drv->of_match_table;

	if (!matches) 
		return 0;

	return of_match_device(matches, dev) != NULL;
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

	match = of_match_device(drv->driver.of_match_table, dev);
	if (match)
		error = drv->probe(macio_dev, match);
	if (error)
		macio_dev_put(macio_dev);

	return error;
}

static void macio_device_remove(struct device *dev)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv = to_macio_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(macio_dev);
	macio_dev_put(macio_dev);
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

static int macio_device_modalias(const struct device *dev, struct kobj_uevent_env *env)
{
	return of_device_uevent_modalias(dev, env);
}

extern const struct attribute_group *macio_dev_groups[];

const struct bus_type macio_bus_type = {
       .name	= "macio",
       .match	= macio_bus_match,
       .uevent	= macio_device_modalias,
       .probe	= macio_device_probe,
       .remove	= macio_device_remove,
       .shutdown = macio_device_shutdown,
       .suspend	= macio_device_suspend,
       .resume	= macio_device_resume,
       .dev_groups = macio_dev_groups,
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
	/* Only quirks for memory resources for now */
	if ((res->flags & IORESOURCE_MEM) == 0)
		return 0;

	/* Grand Central has too large resource 0 on some machines */
	if (index == 0 && of_node_name_eq(np, "gc"))
		res->end = res->start + 0x1ffff;

	/* Airport has bogus resource 2 */
	if (index >= 2 && of_node_name_eq(np, "radio"))
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
	if (of_node_name_eq(np, "escc"))
		return 1;

	/* ESCC has bogus resources >= 3 */
	if (index >= 3 && (of_node_name_eq(np, "ch-a") ||
			   of_node_name_eq(np, "ch-b")))
		return 1;

	/* Media bay has too many resources, keep only first one */
	if (index > 0 && of_node_name_eq(np, "media-bay"))
		return 1;

	/* Some older IDE resources have bogus sizes */
	if (of_node_name_eq(np, "IDE") || of_node_name_eq(np, "ATA") ||
	    of_node_is_type(np, "ide") || of_node_is_type(np, "ata")) {
		if (index == 0 && (res->end - res->start) > 0xfff)
			res->end = res->start + 0xfff;
		if (index == 1 && (res->end - res->start) > 0xff)
			res->end = res->start + 0xff;
	}
	return 0;
}

static void macio_create_fixup_irq(struct macio_dev *dev, int index,
				   unsigned int line)
{
	unsigned int irq;

	irq = irq_create_mapping(NULL, line);
	if (!irq) {
		dev->interrupt[index].start = irq;
		dev->interrupt[index].flags = IORESOURCE_IRQ;
		dev->interrupt[index].name = dev_name(&dev->ofdev.dev);
	}
	if (dev->n_interrupts <= index)
		dev->n_interrupts = index + 1;
}

static void macio_add_missing_resources(struct macio_dev *dev)
{
	struct device_node *np = dev->ofdev.dev.of_node;
	unsigned int irq_base;

	/* Gatwick has some missing interrupts on child nodes */
	if (dev->bus->chip->type != macio_gatwick)
		return;

	/* irq_base is always 64 on gatwick. I have no cleaner way to get
	 * that value from here at this point
	 */
	irq_base = 64;

	/* Fix SCC */
	if (of_node_name_eq(np, "ch-a")) {
		macio_create_fixup_irq(dev, 0, 15 + irq_base);
		macio_create_fixup_irq(dev, 1,  4 + irq_base);
		macio_create_fixup_irq(dev, 2,  5 + irq_base);
		printk(KERN_INFO "macio: fixed SCC irqs on gatwick\n");
	}

	/* Fix media-bay */
	if (of_node_name_eq(np, "media-bay")) {
		macio_create_fixup_irq(dev, 0, 29 + irq_base);
		printk(KERN_INFO "macio: fixed media-bay irq on gatwick\n");
	}

	/* Fix left media bay childs */
	if (dev->media_bay != NULL && of_node_name_eq(np, "floppy")) {
		macio_create_fixup_irq(dev, 0, 19 + irq_base);
		macio_create_fixup_irq(dev, 1,  1 + irq_base);
		printk(KERN_INFO "macio: fixed left floppy irqs\n");
	}
	if (dev->media_bay != NULL && of_node_name_eq(np, "ata4")) {
		macio_create_fixup_irq(dev, 0, 14 + irq_base);
		macio_create_fixup_irq(dev, 0,  3 + irq_base);
		printk(KERN_INFO "macio: fixed left ide irqs\n");
	}
}

static void macio_setup_interrupts(struct macio_dev *dev)
{
	struct device_node *np = dev->ofdev.dev.of_node;
	unsigned int irq;
	int i = 0, j = 0;

	for (;;) {
		struct resource *res;

		if (j >= MACIO_DEV_COUNT_IRQS)
			break;
		res = &dev->interrupt[j];
		irq = irq_of_parse_and_map(np, i++);
		if (!irq)
			break;
		res->start = irq;
		res->flags = IORESOURCE_IRQ;
		res->name = dev_name(&dev->ofdev.dev);
		if (macio_resource_quirks(np, res, i - 1)) {
			memset(res, 0, sizeof(struct resource));
			continue;
		} else
			j++;
	}
	dev->n_interrupts = j;
}

static void macio_setup_resources(struct macio_dev *dev,
				  struct resource *parent_res)
{
	struct device_node *np = dev->ofdev.dev.of_node;
	struct resource r;
	int index;

	for (index = 0; of_address_to_resource(np, index, &r) == 0; index++) {
		struct resource *res;
		if (index >= MACIO_DEV_COUNT_RESOURCES)
			break;
		res = &dev->resource[index];
		*res = r;
		res->name = dev_name(&dev->ofdev.dev);

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
			       index, dev_name(&dev->ofdev.dev));
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
	char name[MAX_NODE_NAME_SIZE + 1];
	struct macio_dev *dev;
	const u32 *reg;

	if (np == NULL)
		return NULL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->bus = &chip->lbus;
	dev->media_bay = in_bay;
	dev->ofdev.dev.of_node = np;
	dev->ofdev.archdata.dma_mask = 0xffffffffUL;
	dev->ofdev.dev.dma_mask = &dev->ofdev.archdata.dma_mask;
	dev->ofdev.dev.coherent_dma_mask = dev->ofdev.archdata.dma_mask;
	dev->ofdev.dev.parent = parent;
	dev->ofdev.dev.bus = &macio_bus_type;
	dev->ofdev.dev.release = macio_release_dev;
	dev->ofdev.dev.dma_parms = &dev->dma_parms;

	/* Standard DMA paremeters */
	dma_set_max_seg_size(&dev->ofdev.dev, 65536);
	dma_set_seg_boundary(&dev->ofdev.dev, 0xffffffff);

#if defined(CONFIG_PCI) && defined(CONFIG_DMA_OPS)
	/* Set the DMA ops to the ones from the PCI device, this could be
	 * fishy if we didn't know that on PowerMac it's always direct ops
	 * or iommu ops that will work fine
	 *
	 * To get all the fields, copy all archdata
	 */
	dev->ofdev.dev.archdata = chip->lbus.pdev->dev.archdata;
	dev->ofdev.dev.dma_ops = chip->lbus.pdev->dev.dma_ops;
#endif /* CONFIG_PCI && CONFIG_DMA_OPS */

#ifdef DEBUG
	printk("preparing mdev @%p, ofdev @%p, dev @%p, kobj @%p\n",
	       dev, &dev->ofdev, &dev->ofdev.dev, &dev->ofdev.dev.kobj);
#endif

	/* MacIO itself has a different reg, we use it's PCI base */
	snprintf(name, sizeof(name), "%pOFn", np);
	if (np == chip->of_node) {
		dev_set_name(&dev->ofdev.dev, "%1d.%08x:%.*s",
			     chip->lbus.index,
#ifdef CONFIG_PCI
			(unsigned int)pci_resource_start(chip->lbus.pdev, 0),
#else
			0, /* NuBus may want to do something better here */
#endif
			MAX_NODE_NAME_SIZE, name);
	} else {
		reg = of_get_property(np, "reg", NULL);
		dev_set_name(&dev->ofdev.dev, "%1d.%08x:%.*s",
			     chip->lbus.index,
			     reg ? *reg : 0, MAX_NODE_NAME_SIZE, name);
	}

	/* Setup interrupts & resources */
	macio_setup_interrupts(dev);
	macio_setup_resources(dev, parent_res);
	macio_add_missing_resources(dev);

	/* Register with core */
	if (of_device_register(&dev->ofdev) != 0) {
		printk(KERN_DEBUG"macio: device registration error for %s!\n",
		       dev_name(&dev->ofdev.dev));
		put_device(&dev->ofdev.dev);
		return NULL;
	}

	return dev;
}

static int macio_skip_device(struct device_node *np)
{
	return of_node_name_prefix(np, "battery") ||
	       of_node_name_prefix(np, "escc-legacy");
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
	for_each_child_of_node(pnode, np) {
		if (macio_skip_device(np))
			continue;
		of_node_get(np);
		mdev = macio_add_one_device(chip, &rdev->ofdev.dev, np, NULL,
					    root_res);
		if (mdev == NULL)
			of_node_put(np);
		else if (of_node_name_prefix(np, "media-bay"))
			mbdev = mdev;
		else if (of_node_name_prefix(np, "escc"))
			sdev = mdev;
	}

	/* Add media bay devices if any */
	if (mbdev) {
		pnode = mbdev->ofdev.dev.of_node;
		for_each_child_of_node(pnode, np) {
			if (macio_skip_device(np))
				continue;
			of_node_get(np);
			if (macio_add_one_device(chip, &mbdev->ofdev.dev, np,
						 mbdev,  root_res) == NULL)
				of_node_put(np);
		}
	}

	/* Add serial ports if any */
	if (sdev) {
		pnode = sdev->ofdev.dev.of_node;
		for_each_child_of_node(pnode, np) {
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
	/* initialize common driver fields */
	drv->driver.bus = &macio_bus_type;

	/* register with core */
	return driver_register(&drv->driver);
}

/**
 * macio_unregister_driver - Unregisters a new MacIO device driver
 * @drv: pointer to the driver definition structure
 */
void macio_unregister_driver(struct macio_driver *drv)
{
	driver_unregister(&drv->driver);
}

/* Managed MacIO resources */
struct macio_devres {
	u32	res_mask;
};

static void maciom_release(struct device *gendev, void *res)
{
	struct macio_dev *dev = to_macio_device(gendev);
	struct macio_devres *dr = res;
	int i, max;

	max = min(dev->n_resources, 32);
	for (i = 0; i < max; i++) {
		if (dr->res_mask & (1 << i))
			macio_release_resource(dev, i);
	}
}

int macio_enable_devres(struct macio_dev *dev)
{
	struct macio_devres *dr;

	dr = devres_find(&dev->ofdev.dev, maciom_release, NULL, NULL);
	if (!dr) {
		dr = devres_alloc(maciom_release, sizeof(*dr), GFP_KERNEL);
		if (!dr)
			return -ENOMEM;
	}
	return devres_get(&dev->ofdev.dev, dr, NULL, NULL) != NULL;
}

static struct macio_devres * find_macio_dr(struct macio_dev *dev)
{
	return devres_find(&dev->ofdev.dev, maciom_release, NULL, NULL);
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
	struct macio_devres *dr = find_macio_dr(dev);

	if (macio_resource_len(dev, resource_no) == 0)
		return 0;
		
	if (!request_mem_region(macio_resource_start(dev, resource_no),
				macio_resource_len(dev, resource_no),
				name))
		goto err_out;

	if (dr && resource_no < 32)
		dr->res_mask |= 1 << resource_no;
	
	return 0;

err_out:
	printk (KERN_WARNING "MacIO: Unable to reserve resource #%d:%lx@%lx"
		" for device %s\n",
		resource_no,
		macio_resource_len(dev, resource_no),
		macio_resource_start(dev, resource_no),
		dev_name(&dev->ofdev.dev));
	return -EBUSY;
}

/**
 * macio_release_resource - Release an MMIO resource
 * @dev: pointer to the device holding the resource
 * @resource_no: resource number to release
 */
void macio_release_resource(struct macio_dev *dev, int resource_no)
{
	struct macio_devres *dr = find_macio_dr(dev);

	if (macio_resource_len(dev, resource_no) == 0)
		return;
	release_mem_region(macio_resource_start(dev, resource_no),
			   macio_resource_len(dev, resource_no));
	if (dr && resource_no < 32)
		dr->res_mask &= ~(1 << resource_no);
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

static int macio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
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

static void macio_pci_remove(struct pci_dev* pdev)
{
	panic("removing of macio-asic not supported !\n");
}

/*
 * MacIO is matched against any Apple ID, it's probe() function
 * will then decide wether it applies or not
 */
static const struct pci_device_id pci_ids[] = { {
	.vendor		= PCI_VENDOR_ID_APPLE,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,

	}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver macio_pci_driver = {
	.name		= "macio",
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
EXPORT_SYMBOL(macio_enable_devres);

