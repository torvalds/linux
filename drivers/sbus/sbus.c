/* sbus.c: SBus support routines.
 *
 * Copyright (C) 1995, 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of_device.h>

#include <asm/system.h>
#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/irq.h>

static ssize_t
show_sbusobppath_attr(struct device * dev, struct device_attribute * attr, char * buf)
{
	struct sbus_dev *sbus;

	sbus = to_sbus_device(dev);

	return snprintf (buf, PAGE_SIZE, "%s\n", sbus->ofdev.node->full_name);
}

static DEVICE_ATTR(obppath, S_IRUSR | S_IRGRP | S_IROTH, show_sbusobppath_attr, NULL);

static void __init fill_sbus_device_iommu(struct sbus_dev *sdev)
{
	struct of_device *op = of_find_device_by_node(sdev->ofdev.node);
	struct dev_archdata *sd, *bus_sd;
	struct sbus_bus *sbus;

	sbus = sdev->bus;
	bus_sd = &sbus->ofdev.dev.archdata;

	sd = &sdev->ofdev.dev.archdata;
	sd->iommu = bus_sd->iommu;
	sd->stc = bus_sd->stc;

	sd = &op->dev.archdata;
	sd->iommu = bus_sd->iommu;
	sd->stc = bus_sd->stc;
}

static void __init fill_sbus_device(struct device_node *dp, struct sbus_dev *sdev)
{
	struct dev_archdata *sd;
	int err;

	sdev->prom_node = dp->node;
	strcpy(sdev->prom_name, dp->name);

	sd = &sdev->ofdev.dev.archdata;
	sd->prom_node = dp;
	sd->op = &sdev->ofdev;

	sdev->ofdev.node = dp;
	if (sdev->parent)
		sdev->ofdev.dev.parent = &sdev->parent->ofdev.dev;
	else
		sdev->ofdev.dev.parent = &sdev->bus->ofdev.dev;
	sdev->ofdev.dev.bus = &sbus_bus_type;
	dev_set_name(&sdev->ofdev.dev, "sbus[%08x]", dp->node);

	if (of_device_register(&sdev->ofdev) != 0)
		printk(KERN_DEBUG "sbus: device registration error for %s!\n",
		       dp->path_component_name);

	/* WE HAVE BEEN INVADED BY ALIENS! */
	err = sysfs_create_file(&sdev->ofdev.dev.kobj, &dev_attr_obppath.attr);

	fill_sbus_device_iommu(sdev);
}

static void __init sdev_insert(struct sbus_dev *sdev, struct sbus_dev **root)
{
	while (*root)
		root = &(*root)->next;
	*root = sdev;
	sdev->next = NULL;
}

static void __init walk_children(struct device_node *dp, struct sbus_dev *parent, struct sbus_bus *sbus)
{
	dp = dp->child;
	while (dp) {
		struct sbus_dev *sdev;

		sdev = kzalloc(sizeof(struct sbus_dev), GFP_ATOMIC);
		if (sdev) {
			sdev_insert(sdev, &parent->child);

			sdev->bus = sbus;
			sdev->parent = parent;

			fill_sbus_device(dp, sdev);

			walk_children(dp, sdev, sbus);
		}
		dp = dp->sibling;
	}
}

static void __init build_one_sbus(struct device_node *dp, int num_sbus)
{
	struct sbus_bus *sbus;
	unsigned int sbus_clock;
	struct device_node *dev_dp;

	sbus = kzalloc(sizeof(struct sbus_bus), GFP_ATOMIC);
	if (!sbus)
		return;

	sbus->prom_node = dp->node;

	sbus_setup_iommu(sbus, dp);

	printk("sbus%d: ", num_sbus);

	sbus_clock = of_getintprop_default(dp, "clock-frequency",
					   (25*1000*1000));
	sbus->clock_freq = sbus_clock;

	printk("Clock %d.%d MHz\n", (int) ((sbus_clock/1000)/1000),
	       (int) (((sbus_clock/1000)%1000 != 0) ? 
		      (((sbus_clock/1000)%1000) + 1000) : 0));

	strcpy(sbus->prom_name, dp->name);

	sbus->ofdev.node = dp;
	sbus->ofdev.dev.parent = NULL;
	sbus->ofdev.dev.bus = &sbus_bus_type;
	dev_set_name(&sbus->ofdev.dev, "sbus%d", num_sbus);

	if (of_device_register(&sbus->ofdev) != 0)
		printk(KERN_DEBUG "sbus: device registration error for %s!\n",
		       dev_name(&sbus->ofdev.dev));

	dev_dp = dp->child;
	while (dev_dp) {
		struct sbus_dev *sdev;

		sdev = kzalloc(sizeof(struct sbus_dev), GFP_ATOMIC);
		if (sdev) {
			sdev_insert(sdev, &sbus->devices);

			sdev->bus = sbus;
			sdev->parent = NULL;
			sdev->ofdev.dev.archdata.iommu =
				sbus->ofdev.dev.archdata.iommu;
			sdev->ofdev.dev.archdata.stc =
				sbus->ofdev.dev.archdata.stc;

			fill_sbus_device(dev_dp, sdev);

			walk_children(dev_dp, sdev, sbus);
		}
		dev_dp = dev_dp->sibling;
	}
}

static int __init sbus_init(void)
{
	struct device_node *dp;
	const char *sbus_name = "sbus";
	int num_sbus = 0;

	if (sbus_arch_preinit())
		return 0;

	if (sparc_cpu_model == sun4d)
		sbus_name = "sbi";

	for_each_node_by_name(dp, sbus_name) {
		build_one_sbus(dp, num_sbus);
		num_sbus++;

	}

	sbus_arch_postinit();

	return 0;
}

subsys_initcall(sbus_init);
