/*
 * Janz CMOD-IO MODULbus Carrier Board PCI Driver
 *
 * Copyright (c) 2010 Ira W. Snyder <iws@ovro.caltech.edu>
 *
 * Lots of inspiration and code was copied from drivers/mfd/sm501.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>

#include <linux/mfd/janz.h>

#define DRV_NAME "janz-cmodio"

/* Size of each MODULbus module in PCI BAR4 */
#define CMODIO_MODULBUS_SIZE	0x200

/* Maximum number of MODULbus modules on a CMOD-IO carrier board */
#define CMODIO_MAX_MODULES	4

/* Module Parameters */
static unsigned int num_modules = CMODIO_MAX_MODULES;
static char *modules[CMODIO_MAX_MODULES] = {
	"empty", "empty", "empty", "empty",
};

module_param_array(modules, charp, &num_modules, S_IRUGO);
MODULE_PARM_DESC(modules, "MODULbus modules attached to the carrier board");

/* Unique Device Id */
static unsigned int cmodio_id;

struct cmodio_device {
	/* Parent PCI device */
	struct pci_dev *pdev;

	/* PLX control registers */
	struct janz_cmodio_onboard_regs __iomem *ctrl;

	/* hex switch position */
	u8 hex;

	/* mfd-core API */
	struct mfd_cell cells[CMODIO_MAX_MODULES];
	struct resource resources[3 * CMODIO_MAX_MODULES];
	struct janz_platform_data pdata[CMODIO_MAX_MODULES];
};

/*
 * Subdevices using the mfd-core API
 */

static int __devinit cmodio_setup_subdevice(struct cmodio_device *priv,
					    char *name, unsigned int devno,
					    unsigned int modno)
{
	struct janz_platform_data *pdata;
	struct mfd_cell *cell;
	struct resource *res;
	struct pci_dev *pci;

	pci = priv->pdev;
	cell = &priv->cells[devno];
	res = &priv->resources[devno * 3];
	pdata = &priv->pdata[devno];

	cell->name = name;
	cell->resources = res;
	cell->num_resources = 3;

	/* Setup the subdevice ID -- must be unique */
	cell->id = cmodio_id++;

	/* Add platform data */
	pdata->modno = modno;
	cell->platform_data = pdata;
	cell->pdata_size = sizeof(*pdata);

	/* MODULbus registers -- PCI BAR3 is big-endian MODULbus access */
	res->flags = IORESOURCE_MEM;
	res->parent = &pci->resource[3];
	res->start = pci->resource[3].start + (CMODIO_MODULBUS_SIZE * modno);
	res->end = res->start + CMODIO_MODULBUS_SIZE - 1;
	res++;

	/* PLX Control Registers -- PCI BAR4 is interrupt and other registers */
	res->flags = IORESOURCE_MEM;
	res->parent = &pci->resource[4];
	res->start = pci->resource[4].start;
	res->end = pci->resource[4].end;
	res++;

	/*
	 * IRQ
	 *
	 * The start and end fields are used as an offset to the irq_base
	 * parameter passed into the mfd_add_devices() function call. All
	 * devices share the same IRQ.
	 */
	res->flags = IORESOURCE_IRQ;
	res->parent = NULL;
	res->start = 0;
	res->end = 0;
	res++;

	return 0;
}

/* Probe each submodule using kernel parameters */
static int __devinit cmodio_probe_submodules(struct cmodio_device *priv)
{
	struct pci_dev *pdev = priv->pdev;
	unsigned int num_probed = 0;
	char *name;
	int i;

	for (i = 0; i < num_modules; i++) {
		name = modules[i];
		if (!strcmp(name, "") || !strcmp(name, "empty"))
			continue;

		dev_dbg(&priv->pdev->dev, "MODULbus %d: name %s\n", i, name);
		cmodio_setup_subdevice(priv, name, num_probed, i);
		num_probed++;
	}

	/* print an error message if no modules were probed */
	if (num_probed == 0) {
		dev_err(&priv->pdev->dev, "no MODULbus modules specified, "
					  "please set the ``modules'' kernel "
					  "parameter according to your "
					  "hardware configuration\n");
		return -ENODEV;
	}

	return mfd_add_devices(&pdev->dev, 0, priv->cells,
			       num_probed, NULL, pdev->irq);
}

/*
 * SYSFS Attributes
 */

static ssize_t mbus_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct cmodio_device *priv = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%x\n", priv->hex);
}

static DEVICE_ATTR(modulbus_number, S_IRUGO, mbus_show, NULL);

static struct attribute *cmodio_sysfs_attrs[] = {
	&dev_attr_modulbus_number.attr,
	NULL,
};

static const struct attribute_group cmodio_sysfs_attr_group = {
	.attrs = cmodio_sysfs_attrs,
};

/*
 * PCI Driver
 */

static int __devinit cmodio_pci_probe(struct pci_dev *dev,
				      const struct pci_device_id *id)
{
	struct cmodio_device *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&dev->dev, "unable to allocate private data\n");
		ret = -ENOMEM;
		goto out_return;
	}

	pci_set_drvdata(dev, priv);
	priv->pdev = dev;

	/* Hardware Initialization */
	ret = pci_enable_device(dev);
	if (ret) {
		dev_err(&dev->dev, "unable to enable device\n");
		goto out_free_priv;
	}

	pci_set_master(dev);
	ret = pci_request_regions(dev, DRV_NAME);
	if (ret) {
		dev_err(&dev->dev, "unable to request regions\n");
		goto out_pci_disable_device;
	}

	/* Onboard configuration registers */
	priv->ctrl = pci_ioremap_bar(dev, 4);
	if (!priv->ctrl) {
		dev_err(&dev->dev, "unable to remap onboard regs\n");
		ret = -ENOMEM;
		goto out_pci_release_regions;
	}

	/* Read the hex switch on the carrier board */
	priv->hex = ioread8(&priv->ctrl->int_enable);

	/* Add the MODULbus number (hex switch value) to the device's sysfs */
	ret = sysfs_create_group(&dev->dev.kobj, &cmodio_sysfs_attr_group);
	if (ret) {
		dev_err(&dev->dev, "unable to create sysfs attributes\n");
		goto out_unmap_ctrl;
	}

	/*
	 * Disable all interrupt lines, each submodule will enable its
	 * own interrupt line if needed
	 */
	iowrite8(0xf, &priv->ctrl->int_disable);

	/* Register drivers for all submodules */
	ret = cmodio_probe_submodules(priv);
	if (ret) {
		dev_err(&dev->dev, "unable to probe submodules\n");
		goto out_sysfs_remove_group;
	}

	return 0;

out_sysfs_remove_group:
	sysfs_remove_group(&dev->dev.kobj, &cmodio_sysfs_attr_group);
out_unmap_ctrl:
	iounmap(priv->ctrl);
out_pci_release_regions:
	pci_release_regions(dev);
out_pci_disable_device:
	pci_disable_device(dev);
out_free_priv:
	kfree(priv);
out_return:
	return ret;
}

static void __devexit cmodio_pci_remove(struct pci_dev *dev)
{
	struct cmodio_device *priv = pci_get_drvdata(dev);

	mfd_remove_devices(&dev->dev);
	sysfs_remove_group(&dev->dev.kobj, &cmodio_sysfs_attr_group);
	iounmap(priv->ctrl);
	pci_release_regions(dev);
	pci_disable_device(dev);
	kfree(priv);
}

#define PCI_VENDOR_ID_JANZ		0x13c3

/* The list of devices that this module will support */
static DEFINE_PCI_DEVICE_TABLE(cmodio_pci_ids) = {
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_JANZ, 0x0101 },
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050, PCI_VENDOR_ID_JANZ, 0x0100 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, cmodio_pci_ids);

static struct pci_driver cmodio_pci_driver = {
	.name     = DRV_NAME,
	.id_table = cmodio_pci_ids,
	.probe    = cmodio_pci_probe,
	.remove   = __devexit_p(cmodio_pci_remove),
};

/*
 * Module Init / Exit
 */

static int __init cmodio_init(void)
{
	return pci_register_driver(&cmodio_pci_driver);
}

static void __exit cmodio_exit(void)
{
	pci_unregister_driver(&cmodio_pci_driver);
}

MODULE_AUTHOR("Ira W. Snyder <iws@ovro.caltech.edu>");
MODULE_DESCRIPTION("Janz CMOD-IO PCI MODULbus Carrier Board Driver");
MODULE_LICENSE("GPL");

module_init(cmodio_init);
module_exit(cmodio_exit);
