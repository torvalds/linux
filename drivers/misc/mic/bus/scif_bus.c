/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel Symmetric Communications Interface Bus driver.
 */
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/dma-mapping.h>

#include "scif_bus.h"

static ssize_t device_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct scif_hw_dev *dev = dev_to_scif(d);

	return sprintf(buf, "0x%04x\n", dev->id.device);
}
static DEVICE_ATTR_RO(device);

static ssize_t vendor_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct scif_hw_dev *dev = dev_to_scif(d);

	return sprintf(buf, "0x%04x\n", dev->id.vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t modalias_show(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct scif_hw_dev *dev = dev_to_scif(d);

	return sprintf(buf, "scif:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *scif_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(scif_dev);

static inline int scif_id_match(const struct scif_hw_dev *dev,
				const struct scif_hw_dev_id *id)
{
	if (id->device != dev->id.device && id->device != SCIF_DEV_ANY_ID)
		return 0;

	return id->vendor == SCIF_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

/*
 * This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call scif_dev_probe().
 */
static int scif_dev_match(struct device *dv, struct device_driver *dr)
{
	unsigned int i;
	struct scif_hw_dev *dev = dev_to_scif(dv);
	const struct scif_hw_dev_id *ids;

	ids = drv_to_scif(dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (scif_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int scif_uevent(struct device *dv, struct kobj_uevent_env *env)
{
	struct scif_hw_dev *dev = dev_to_scif(dv);

	return add_uevent_var(env, "MODALIAS=scif:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

static int scif_dev_probe(struct device *d)
{
	struct scif_hw_dev *dev = dev_to_scif(d);
	struct scif_driver *drv = drv_to_scif(dev->dev.driver);

	return drv->probe(dev);
}

static int scif_dev_remove(struct device *d)
{
	struct scif_hw_dev *dev = dev_to_scif(d);
	struct scif_driver *drv = drv_to_scif(dev->dev.driver);

	drv->remove(dev);
	return 0;
}

static struct bus_type scif_bus = {
	.name  = "scif_bus",
	.match = scif_dev_match,
	.dev_groups = scif_dev_groups,
	.uevent = scif_uevent,
	.probe = scif_dev_probe,
	.remove = scif_dev_remove,
};

int scif_register_driver(struct scif_driver *driver)
{
	driver->driver.bus = &scif_bus;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(scif_register_driver);

void scif_unregister_driver(struct scif_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(scif_unregister_driver);

static void scif_release_dev(struct device *d)
{
	struct scif_hw_dev *sdev = dev_to_scif(d);

	kfree(sdev);
}

struct scif_hw_dev *
scif_register_device(struct device *pdev, int id, struct dma_map_ops *dma_ops,
		     struct scif_hw_ops *hw_ops, u8 dnode, u8 snode,
		     struct mic_mw *mmio, struct mic_mw *aper, void *dp,
		     void __iomem *rdp, struct dma_chan **chan, int num_chan,
		     bool card_rel_da)
{
	int ret;
	struct scif_hw_dev *sdev;

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return ERR_PTR(-ENOMEM);

	sdev->dev.parent = pdev;
	sdev->id.device = id;
	sdev->id.vendor = SCIF_DEV_ANY_ID;
	sdev->dev.archdata.dma_ops = dma_ops;
	sdev->dev.release = scif_release_dev;
	sdev->hw_ops = hw_ops;
	sdev->dnode = dnode;
	sdev->snode = snode;
	dev_set_drvdata(&sdev->dev, sdev);
	sdev->dev.bus = &scif_bus;
	sdev->mmio = mmio;
	sdev->aper = aper;
	sdev->dp = dp;
	sdev->rdp = rdp;
	sdev->dev.dma_mask = &sdev->dev.coherent_dma_mask;
	dma_set_mask(&sdev->dev, DMA_BIT_MASK(64));
	sdev->dma_ch = chan;
	sdev->num_dma_ch = num_chan;
	sdev->card_rel_da = card_rel_da;
	dev_set_name(&sdev->dev, "scif-dev%u", sdev->dnode);
	/*
	 * device_register() causes the bus infrastructure to look for a
	 * matching driver.
	 */
	ret = device_register(&sdev->dev);
	if (ret)
		goto free_sdev;
	return sdev;
free_sdev:
	kfree(sdev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(scif_register_device);

void scif_unregister_device(struct scif_hw_dev *sdev)
{
	device_unregister(&sdev->dev);
}
EXPORT_SYMBOL_GPL(scif_unregister_device);

static int __init scif_init(void)
{
	return bus_register(&scif_bus);
}

static void __exit scif_exit(void)
{
	bus_unregister(&scif_bus);
}

core_initcall(scif_init);
module_exit(scif_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) SCIF Bus driver");
MODULE_LICENSE("GPL v2");
